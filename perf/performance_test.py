'''
Copyright (C) 2020-2026  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.

Real-world performance benchmarks
'''
import argparse
import bisect
import gc
import gzip
import json
import platform
import random
import statistics
import sys
from collections import deque
from decimal import Decimal
from pathlib import Path
from time import perf_counter_ns

from order_book import OrderBook

from pyorderbook import OrderBook as PyOrderBook

try:
    from sortedcontainers import SortedDict as ScSortedDict
except ImportError:
    ScSortedDict = None


DATA_DIR = Path(__file__).resolve().parent / 'data'
READ_EVERY = 10
TP_PASSES = 5
RANK_MEAN = 64.0
DEEP_FRACTION = 0.1
FLICKER = 0.6


# data loading

def load_snapshot(name):
    path = DATA_DIR / f'{name}.json.gz'
    if not path.exists():
        sys.exit(f'{path} not found -- run perf/capture.py first')
    with gzip.open(path, 'rt', encoding='utf-8') as f:
        return json.load(f)


def l2_levels(snap, depth=None):
    '''{'bid': [(Decimal price, Decimal size) best-first], 'ask': [...]}'''
    out = {}
    for side, key in (('bid', 'bids'), ('ask', 'asks')):
        entries = snap[key][:depth] if depth else snap[key]
        out[side] = [(Decimal(p), Decimal(s)) for p, s, _ in entries]
    return out


def l3_levels(snap, depth=None):
    '''same shape but price -> {order id: size}, preserving the exchange's queue order'''
    out = {}
    for side, key in (('bid', 'bids'), ('ask', 'asks')):
        levels = {}
        for p, s, oid in snap[key]:
            price = Decimal(p)
            if price not in levels:
                if depth and len(levels) == depth:
                    break
                levels[price] = {}
            levels[price][oid] = Decimal(s)
        out[side] = list(levels.items())
    return out


# event stream synthesis

class _LiveSide:
    '''one side during generation: ascending price list, membership set, and
    recently vacated/opened prices for quote flicker'''

    def __init__(self, prices, is_bid):
        self.prices = sorted(prices)
        self.present = set(prices)
        self.is_bid = is_bid
        self.initial = len(prices)
        self.floor = len(prices) // 2
        self.cap = len(prices) * 2
        self.vacated = deque(maxlen=32)
        self.opened = deque(maxlen=32)

    def pos(self, rank):
        '''rank 0 = best. bids ascend toward the end of the list'''
        return len(self.prices) - 1 - rank if self.is_bid else rank

    def pick_rank(self, rng):
        '''mostly exponential near the top of book, uniform remainder so deep
        levels see occasional traffic'''
        if rng.random() < DEEP_FRACTION:
            return rng.randrange(len(self.prices))
        return int(rng.expovariate(1 / RANK_MEAN))

    def price_at_rank(self, rank):
        return self.prices[self.pos(min(rank, len(self.prices) - 1))]

    def best(self):
        return self.prices[-1] if self.is_bid else self.prices[0]

    def add(self, price):
        bisect.insort(self.prices, price)
        self.present.add(price)
        self.opened.append(price)

    def remove(self, price):
        self.prices.pop(bisect.bisect_left(self.prices, price))
        self.present.discard(price)
        self.vacated.append(price)

    def crosses(self, price, other):
        return price >= other.best() if self.is_bid else price <= other.best()

    def pick_new_price(self, rng, other, tick):
        '''level birth: a recently vacated price (flicker) or a fresh tick-aligned
        price near a rank-weighted anchor. may improve the best but never crosses
        the spread. None if nothing nearby is free'''
        if self.vacated and rng.random() < FLICKER:
            price = self.vacated.pop()
            if price not in self.present and not self.crosses(price, other):
                return price
        anchor = self.price_at_rank(self.pick_rank(rng))
        inward = 1 if self.is_bid else -1
        for _ in range(8):
            step = rng.randint(1, 8) * (inward if rng.random() < 0.5 else -inward)
            candidate = anchor + tick * step
            if candidate in self.present or self.crosses(candidate, other):
                continue
            return candidate
        return None

    def pick_doomed_price(self, rng):
        '''level death: a recently opened level (flicker) or rank-weighted'''
        while self.opened and rng.random() < FLICKER:
            price = self.opened.pop()
            if price in self.present:
                return price
        return self.price_at_rank(self.pick_rank(rng))


def gen_l2_events(levels, n, seed, tick):
    '''(kind, side, price, size), kind 0 = set, 1 = delete. 70% size updates;
    adds and deletes split by a mean-reverting bias so depth stays put'''
    rng = random.Random(seed)
    sizes = [s for lv in levels.values() for _, s in lv]
    size_pool = [rng.choice(sizes) for _ in range(1024)]
    sides = {
        'bid': _LiveSide([p for p, _ in levels['bid']], True),
        'ask': _LiveSide([p for p, _ in levels['ask']], False),
    }
    events = []
    for i in range(n):
        name = 'bid' if rng.random() < 0.5 else 'ask'
        side, other = sides[name], sides['ask' if name == 'bid' else 'bid']
        size = size_pool[i & 1023]
        if rng.random() < 0.70:
            events.append((0, name, side.price_at_rank(side.pick_rank(rng)), size))
            continue
        bias = (side.initial - len(side.prices)) / side.initial
        p_add = min(0.9, max(0.1, 0.5 + bias))
        if rng.random() < p_add and len(side.prices) < side.cap:
            price = side.pick_new_price(rng, other, tick)
            if price is None:
                events.append((0, name, side.price_at_rank(side.pick_rank(rng)), size))
            else:
                side.add(price)
                events.append((0, name, price, size))
        elif len(side.prices) > side.floor:
            price = side.pick_doomed_price(rng)
            side.remove(price)
            events.append((1, name, price, None))
        else:
            events.append((0, name, side.price_at_rank(side.pick_rank(rng)), size))
    return events


def gen_l3_events(levels, n, seed, tick):
    '''(kind, side, price, order id, size), kind 0 = add, 1 = remove the oldest
    order at that price. adds and removes split by a mean-reverting bias so the
    order population stays put. 20% of adds open a fresh price, and a remove
    that empties its level deletes the level'''
    rng = random.Random(seed)
    sizes = [s for lv in levels.values() for _, orders in lv for s in orders.values()]
    size_pool = [rng.choice(sizes) for _ in range(1024)]
    sides = {
        'bid': _LiveSide([p for p, _ in levels['bid']], True),
        'ask': _LiveSide([p for p, _ in levels['ask']], False),
    }
    counts = {name: {p: len(orders) for p, orders in levels[name]}
              for name in ('bid', 'ask')}
    target = {name: sum(counts[name].values()) for name in ('bid', 'ask')}
    orders = dict(target)
    events = []
    for i in range(n):
        name = 'bid' if rng.random() < 0.5 else 'ask'
        side, other = sides[name], sides['ask' if name == 'bid' else 'bid']
        count = counts[name]
        size = size_pool[i & 1023]
        bias = (target[name] - orders[name]) / target[name]
        p_add = min(0.9, max(0.1, 0.5 + bias))
        if rng.random() < p_add:
            price = None
            fresh = 1.0 if len(side.prices) <= side.floor else 0.20
            if rng.random() < fresh and len(side.prices) < side.cap:
                price = side.pick_new_price(rng, other, tick)
            if price is None:
                price = side.price_at_rank(side.pick_rank(rng))
                count[price] += 1
            else:
                side.add(price)
                count[price] = 1
            orders[name] += 1
            events.append((0, name, price, f'sim-{i}', size))
        else:
            price = side.pick_doomed_price(rng)
            if count[price] == 1 and len(side.prices) <= side.floor:
                # the level is floor protected, join its queue instead
                count[price] += 1
                orders[name] += 1
                events.append((0, name, price, f'sim-{i}', size))
            else:
                count[price] -= 1
                orders[name] -= 1
                if not count[price]:
                    del count[price]
                    side.remove(price)
                events.append((1, name, price, None, None))
    return events


# measurement

def timer_resolution():
    ticks = set()
    for _ in range(2000):
        t0 = perf_counter_ns()
        t1 = perf_counter_ns()
        if t1 > t0:
            ticks.add(t1 - t0)
    return min(ticks) if ticks else 1


def percentiles(samples):
    s = sorted(samples)

    def pick(q):
        return s[min(len(s) - 1, int(q * len(s)))]

    return {'p50': pick(0.50), 'p99': pick(0.99),
            'p99.9': pick(0.999) if len(s) >= 10_000 else None, 'max': s[-1]}


def measure(build_and_replay, events):
    '''median-of-passes throughput, then one instrumented pass for latencies'''
    totals = []
    for _ in range(TP_PASSES):
        gc.disable()
        totals.append(build_and_replay(events, None))
        gc.enable()
    samples = []
    gc.disable()
    build_and_replay(events, samples)
    gc.enable()
    ns_per_op = statistics.median(totals) / len(events)
    return {'ops': len(events), 'ns_per_op': ns_per_op,
            'ops_per_sec': 1e9 / ns_per_op, **percentiles(samples)}


# L2 replay

def l2_c(levels):
    def run(events, samples):
        ob = OrderBook()
        book = {'bid': ob.bids, 'ask': ob.asks}
        for side in ('bid', 'ask'):
            b = book[side]
            for price, size in levels[side]:
                b[price] = size
            b.index(0)
        if samples is None:
            t0 = perf_counter_ns()
            for i, (kind, side, price, size) in enumerate(events):
                b = book[side]
                if kind:
                    del b[price]
                else:
                    b[price] = size
                if not i % READ_EVERY:
                    b.index(0)
            return perf_counter_ns() - t0
        append = samples.append
        for i, (kind, side, price, size) in enumerate(events):
            t0 = perf_counter_ns()
            b = book[side]
            if kind:
                del b[price]
            else:
                b[price] = size
            if not i % READ_EVERY:
                b.index(0)
            append(perf_counter_ns() - t0)
    return run


def l2_python(levels):
    def run(events, samples):
        ob = PyOrderBook()
        book = {'bid': ob.bids, 'ask': ob.asks}
        for side in ('bid', 'ask'):
            b = book[side]
            for price, size in levels[side]:
                b[price] = size
            b.index(0)
        if samples is None:
            t0 = perf_counter_ns()
            for i, (kind, side, price, size) in enumerate(events):
                b = book[side]
                if kind:
                    del b[price]
                else:
                    b[price] = size
                if not i % READ_EVERY:
                    b.index(0)
            return perf_counter_ns() - t0
        append = samples.append
        for i, (kind, side, price, size) in enumerate(events):
            t0 = perf_counter_ns()
            b = book[side]
            if kind:
                del b[price]
            else:
                b[price] = size
            if not i % READ_EVERY:
                b.index(0)
            append(perf_counter_ns() - t0)
    return run


def l2_sc(levels):
    def run(events, samples):
        book = {'bid': (ScSortedDict(), -1), 'ask': (ScSortedDict(), 0)}
        for side in ('bid', 'ask'):
            b, best = book[side]
            for price, size in levels[side]:
                b[price] = size
            b.peekitem(best)
        if samples is None:
            t0 = perf_counter_ns()
            for i, (kind, side, price, size) in enumerate(events):
                b, best = book[side]
                if kind:
                    del b[price]
                else:
                    b[price] = size
                if not i % READ_EVERY:
                    b.peekitem(best)
            return perf_counter_ns() - t0
        append = samples.append
        for i, (kind, side, price, size) in enumerate(events):
            t0 = perf_counter_ns()
            b, best = book[side]
            if kind:
                del b[price]
            else:
                b[price] = size
            if not i % READ_EVERY:
                b.peekitem(best)
            append(perf_counter_ns() - t0)
    return run


# L3 replay

def l3_c(levels):
    def run(events, samples):
        ob = OrderBook()
        book = {'bid': ob.bids, 'ask': ob.asks}
        for side in ('bid', 'ask'):
            b = book[side]
            for price, orders in levels[side]:
                b[price] = dict(orders)
            b.index(0)
        if samples is None:
            t0 = perf_counter_ns()
            for i, (kind, side, price, oid, size) in enumerate(events):
                b = book[side]
                if kind:
                    orders = b[price]
                    orders.pop(next(iter(orders)))
                    if not orders:
                        del b[price]
                elif price in b:
                    b[price][oid] = size
                else:
                    b[price] = {oid: size}
                if not i % READ_EVERY:
                    b.index(0)
            return perf_counter_ns() - t0
        append = samples.append
        for i, (kind, side, price, oid, size) in enumerate(events):
            t0 = perf_counter_ns()
            b = book[side]
            if kind:
                orders = b[price]
                orders.pop(next(iter(orders)))
                if not orders:
                    del b[price]
            elif price in b:
                b[price][oid] = size
            else:
                b[price] = {oid: size}
            if not i % READ_EVERY:
                b.index(0)
            append(perf_counter_ns() - t0)
    return run


def l3_python(levels):
    def run(events, samples):
        ob = PyOrderBook()
        book = {'bid': ob.bids, 'ask': ob.asks}
        for side in ('bid', 'ask'):
            b = book[side]
            for price, orders in levels[side]:
                b[price] = dict(orders)
            b.index(0)
        if samples is None:
            t0 = perf_counter_ns()
            for i, (kind, side, price, oid, size) in enumerate(events):
                b = book[side]
                if kind:
                    orders = b[price]
                    orders.pop(next(iter(orders)))
                    if not orders:
                        del b[price]
                elif price in b:
                    b[price][oid] = size
                else:
                    b[price] = {oid: size}
                if not i % READ_EVERY:
                    b.index(0)
            return perf_counter_ns() - t0
        append = samples.append
        for i, (kind, side, price, oid, size) in enumerate(events):
            t0 = perf_counter_ns()
            b = book[side]
            if kind:
                orders = b[price]
                orders.pop(next(iter(orders)))
                if not orders:
                    del b[price]
            elif price in b:
                b[price][oid] = size
            else:
                b[price] = {oid: size}
            if not i % READ_EVERY:
                b.index(0)
            append(perf_counter_ns() - t0)
    return run


def l3_sc(levels):
    def run(events, samples):
        book = {'bid': (ScSortedDict(), -1), 'ask': (ScSortedDict(), 0)}
        for side in ('bid', 'ask'):
            b, best = book[side]
            for price, orders in levels[side]:
                b[price] = dict(orders)
            b.peekitem(best)
        if samples is None:
            t0 = perf_counter_ns()
            for i, (kind, side, price, oid, size) in enumerate(events):
                b, best = book[side]
                if kind:
                    orders = b[price]
                    orders.pop(next(iter(orders)))
                    if not orders:
                        del b[price]
                elif price in b:
                    b[price][oid] = size
                else:
                    b[price] = {oid: size}
                if not i % READ_EVERY:
                    b.peekitem(best)
            return perf_counter_ns() - t0
        append = samples.append
        for i, (kind, side, price, oid, size) in enumerate(events):
            t0 = perf_counter_ns()
            b, best = book[side]
            if kind:
                orders = b[price]
                orders.pop(next(iter(orders)))
                if not orders:
                    del b[price]
            elif price in b:
                b[price][oid] = size
            else:
                b[price] = {oid: size}
            if not i % READ_EVERY:
                b.peekitem(best)
            append(perf_counter_ns() - t0)
    return run


# snapshot build

def snapshot_builders(levels):
    def build_c():
        ob = OrderBook()
        for price, size in levels['bid']:
            ob.bids[price] = size
        for price, size in levels['ask']:
            ob.asks[price] = size
        return ob.to_dict()

    def build_python():
        ob = PyOrderBook()
        for price, size in levels['bid']:
            ob.bids[price] = size
        for price, size in levels['ask']:
            ob.asks[price] = size
        return ob.to_dict()

    def build_sc():
        bids, asks = ScSortedDict(), ScSortedDict()
        for price, size in levels['bid']:
            bids[price] = size
        for price, size in levels['ask']:
            asks[price] = size
        return {'bid': {p: bids[p] for p in reversed(bids)},
                'ask': dict(asks)}

    return build_c, build_sc, build_python


def time_build(build, repeats=5):
    samples = []
    for _ in range(repeats):
        gc.disable()
        t0 = perf_counter_ns()
        build()
        samples.append(perf_counter_ns() - t0)
        gc.enable()
    return statistics.median(samples)


# checksums

def run_checksums(snap):
    results = {}
    for fmt, depth in (('KRAKEN', 10), ('OKX', 25), ('BITGET', 25), ('BITFINEX', 25)):
        ob = OrderBook(checksum_format=fmt)
        for p, s, _ in snap['bids'][:depth]:
            ob.bids[Decimal(p)] = Decimal(s)
        for p, s, _ in snap['asks'][:depth]:
            ob.asks[Decimal(p)] = Decimal(s)
        ob.checksum()
        samples = []
        for _ in range(TP_PASSES):
            gc.disable()
            t0 = perf_counter_ns()
            for _ in range(20_000):
                ob.checksum()
            samples.append((perf_counter_ns() - t0) / 20_000)
            gc.enable()
        results[fmt] = statistics.median(samples)
    return results


# reporting

def fmt_ns(ns):
    if ns is None:
        return '-'
    if ns < 1_000:
        return f'{ns:,.0f} ns'
    if ns < 1_000_000:
        return f'{ns / 1_000:,.2f} µs'
    if ns < 1_000_000_000:
        return f'{ns / 1_000_000:,.2f} ms'
    return f'{ns / 1_000_000_000:,.2f} s'


def fmt_rate(ops_per_sec):
    if ops_per_sec >= 1e6:
        return f'{ops_per_sec / 1e6:,.1f}M/s'
    if ops_per_sec >= 1e3:
        return f'{ops_per_sec / 1e3:,.0f}K/s'
    return f'{ops_per_sec:,.0f}/s'


def print_replay_table(title, rows):
    print(f'\n{title}')
    header = f'{"library":<18}{"events":>10}{"ns/op":>10}{"throughput":>12}' \
             f'{"p50":>10}{"p99":>12}{"p99.9":>12}{"max":>12}'
    print(header)
    print('-' * len(header))
    base = rows[0][1]['ns_per_op']
    for name, r in rows:
        rel = '' if r['ns_per_op'] == base else f'  ({r["ns_per_op"] / base:,.1f}x slower)'
        print(f'{name:<18}{r["ops"]:>10,}{r["ns_per_op"]:>10,.0f}'
              f'{fmt_rate(r["ops_per_sec"]):>12}{fmt_ns(r["p50"]):>10}'
              f'{fmt_ns(r["p99"]):>12}{fmt_ns(r["p99.9"]):>12}{fmt_ns(r["max"]):>12}{rel}')


def main():
    parser = argparse.ArgumentParser(
        description='replay real Coinbase order book activity against each implementation')
    parser.add_argument('--scenario', choices=['snapshot', 'l2', 'l3', 'checksum', 'all'],
                        default='all')
    parser.add_argument('--ops', type=int, default=200_000,
                        help='events per replay for order_book / sortedcontainers (default 200k)')
    parser.add_argument('--python-ops', type=int, default=20_000,
                        help='events for the pure python book (default 20k; it is slow)')
    parser.add_argument('--depth', type=int, default=2_000,
                        help='price levels per side in the replay window (default 2000)')
    parser.add_argument('--seed', type=int, default=42)
    parser.add_argument('--tick', default='0.01', help='price increment (default 0.01)')
    parser.add_argument('--json', metavar='PATH', help='also dump results as JSON')
    args = parser.parse_args()
    tick = Decimal(args.tick)

    l2_snap = load_snapshot('l2_snapshot')
    l3_snap = load_snapshot('l3_snapshot')
    resolution = timer_resolution()
    results = {'meta': {
        'product': l2_snap['product'], 'captured_at': l2_snap['captured_at'],
        'seed': args.seed, 'ops': args.ops, 'python_ops': args.python_ops,
        'depth': args.depth, 'python': platform.python_version(),
        'machine': platform.machine(), 'timer_resolution_ns': resolution,
        'so': sys.modules['order_book'].__file__,
    }}

    print(f'order_book real-data benchmark -- {l2_snap["product"]} snapshot '
          f'captured {l2_snap["captured_at"]}')
    print(f'python {platform.python_version()} on {platform.machine()}, '
          f'seed {args.seed}, timer resolution ~{resolution} ns '
          f'(latency percentiles are quantized to it)')
    if ScSortedDict is None:
        print('sortedcontainers not installed -- skipping that column')

    if args.scenario in ('snapshot', 'all'):
        levels = l2_levels(l2_snap)
        n = len(levels['bid']) + len(levels['ask'])
        build_c, build_sc, build_python = snapshot_builders(levels)
        rows = [('order_book', time_build(build_c))]
        if ScSortedDict:
            rows.append(('sortedcontainers', time_build(build_sc)))
        rows.append(('pure python', time_build(build_python)))
        print(f'\nsnapshot: build sorted book from full L2 snapshot ({n:,} levels) + export')
        base = rows[0][1]
        for name, ns in rows:
            rel = '' if ns == base else f'  ({ns / base:.1f}x slower)'
            print(f'  {name:<18}{fmt_ns(ns):>12}{rel}')
        results['snapshot'] = {name: ns for name, ns in rows}

    if args.scenario in ('l2', 'all'):
        levels = l2_levels(l2_snap, args.depth)
        events = gen_l2_events(levels, args.ops, args.seed, tick)
        rows = [('order_book', measure(l2_c(levels), events))]
        if ScSortedDict:
            rows.append(('sortedcontainers', measure(l2_sc(levels), events)))
        rows.append(('pure python',
                     measure(l2_python(levels), events[:args.python_ops])))
        print_replay_table(
            f'L2 replay: level set/delete stream, top {args.depth:,} levels/side, '
            f'top-of-book read every {READ_EVERY} events', rows)
        results['l2'] = dict(rows)

    if args.scenario in ('l3', 'all'):
        levels = l3_levels(l3_snap, args.depth)
        n_orders = sum(len(o) for _, o in levels['bid']) + sum(len(o) for _, o in levels['ask'])
        events = gen_l3_events(levels, args.ops, args.seed, tick)
        rows = [('order_book', measure(l3_c(levels), events))]
        if ScSortedDict:
            rows.append(('sortedcontainers', measure(l3_sc(levels), events)))
        rows.append(('pure python',
                     measure(l3_python(levels), events[:args.python_ops])))
        print_replay_table(
            f'L3 replay: per-order add/remove stream, {n_orders:,} real resting orders, '
            f'top-of-book read every {READ_EVERY} events', rows)
        results['l3'] = dict(rows)

    if args.scenario in ('checksum', 'all'):
        checksums = run_checksums(l2_snap)
        print('\nexchange checksum on the real book (order_book only):')
        for fmt, ns in checksums.items():
            print(f'  {fmt:<18}{fmt_ns(ns):>12} per checksum()')
        results['checksum'] = checksums

    if args.json:
        Path(args.json).write_text(json.dumps(results, indent=2, default=float))
        print(f'\nresults written to {args.json}')


if __name__ == '__main__':
    main()
