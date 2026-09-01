'''
Copyright (C) 2020-2026  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.
'''
import random
import sys
import sysconfig
import threading
from decimal import Decimal

import pytest

from order_book import OrderBook, SortedDict


FREE_THREADED = bool(sysconfig.get_config_var('Py_GIL_DISABLED'))
THREADS = 8
ITERATIONS = 5000


def run_threads(target, count=THREADS):
    barrier = threading.Barrier(count)
    errors = []

    def wrapped(seed):
        barrier.wait()
        try:
            target(seed)
        except Exception as e:  # noqa: BLE001
            errors.append(e)

    threads = [threading.Thread(target=wrapped, args=(seed,)) for seed in range(count)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()

    assert not errors, errors


def check_side(side, reverse):
    keys = side.keys()
    assert list(keys) == sorted(keys, reverse=reverse)
    assert list(keys) == sorted(side.to_dict().keys(), reverse=reverse)
    assert [k for k, _ in side.to_list()] == list(keys)
    assert len(keys) == len(side)
    for key in keys:
        assert key in side
        side[key]


def read_side(side):
    side.keys()
    if len(side):
        try:
            side.index(0)
        except IndexError:
            pass
    try:
        list(side.items())
    except KeyError:
        pass
    side.to_list()


@pytest.mark.skipif(not FREE_THREADED, reason='free-threaded build only')
def test_import_leaves_gil_disabled():
    assert not sys._is_gil_enabled()


def test_shared_sorteddict_churn():
    sd = SortedDict(ordering='DESC')

    def churn(seed):
        rng = random.Random(seed)
        for i in range(ITERATIONS):
            key = Decimal(rng.randint(1, 300))
            r = rng.random()
            if r < 0.5:
                sd[key] = Decimal(i)
            elif r < 0.8:
                try:
                    del sd[key]
                except KeyError:
                    pass
            else:
                read_side(sd)

    run_threads(churn)
    check_side(sd, reverse=True)


def test_shared_sorteddict_truncate():
    depth = 20
    sd = SortedDict(ordering='ASC', max_depth=depth, truncate=True)

    def churn(seed):
        rng = random.Random(seed)
        for i in range(ITERATIONS):
            sd[Decimal(rng.randint(1, 1000))] = Decimal(i)
            if i % 50 == 0:
                sd.truncate()
                read_side(sd)

    run_threads(churn)
    assert len(getattr(sd, '__data')) <= depth
    check_side(sd, reverse=False)


@pytest.mark.parametrize('fmt', ['KRAKEN', 'OKX', 'BITFINEX'])
def test_shared_orderbook_writers_and_readers(fmt):
    ob = OrderBook(checksum_format=fmt, max_depth=50)

    def writer(seed):
        rng = random.Random(seed)
        for i in range(ITERATIONS):
            side = ob.bids if rng.random() < 0.5 else ob.asks
            price = Decimal(rng.randint(1, 500))
            r = rng.random()
            if r < 0.6:
                side[price] = Decimal(i)
            elif r < 0.95:
                try:
                    del side[price]
                except KeyError:
                    pass
            else:
                # replace a whole side
                ob['bid' if side is ob.bids else 'ask'] = {Decimal(p): Decimal(1) for p in range(1, 30)}

    def reader(seed):
        for _ in range(ITERATIONS // 5):
            assert isinstance(ob.checksum(), int)
            d = ob.to_dict()
            assert set(d) == {'bid', 'ask'}
            len(ob)
            read_side(ob.bids)
            read_side(ob.asks)

    def worker(seed):
        if seed % 2:
            reader(seed)
        else:
            writer(seed)

    run_threads(worker)
    check_side(ob.bids, reverse=True)
    check_side(ob.asks, reverse=False)
    assert isinstance(ob.checksum(), int)


def test_shared_iterator():
    sd = SortedDict(ordering='ASC')
    for i in range(2000):
        sd[Decimal(i)] = Decimal(i)

    it = sd.items()
    seen = []
    lock = threading.Lock()

    def drain(seed):
        for pair in it:
            with lock:
                seen.append(pair)

    run_threads(drain)
    assert set(seen) == set(sd.to_list())
