'''
Copyright (C) 2020-2026  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.


Capture real order book snapshots from Coinbase and cache them in perf/data/
'''
import argparse
import gzip
import json
import time
from collections import OrderedDict
from pathlib import Path

import requests


DATA_DIR = Path(__file__).resolve().parent / 'data'
API = 'https://api.exchange.coinbase.com/products/{product}/book?level={level}'


def truncate_l3(entries, depth):
    '''Keep every order belonging to the first `depth` distinct price levels.
    Coinbase returns orders best-first, so the first N distinct prices seen
    are the N most competitive levels.'''
    levels = OrderedDict()
    for price, size, order_id in entries:
        if price not in levels and len(levels) == depth:
            break
        levels.setdefault(price, []).append([price, size, order_id])
    return [order for orders in levels.values() for order in orders]


def capture(product, l3_depth):
    DATA_DIR.mkdir(exist_ok=True)
    for level, name in ((2, 'l2_snapshot'), (3, 'l3_snapshot')):
        resp = requests.get(API.format(product=product, level=level), timeout=30)
        resp.raise_for_status()
        book = resp.json()

        bids, asks = book['bids'], book['asks']
        truncated = False
        if level == 3 and l3_depth:
            bids = truncate_l3(bids, l3_depth)
            asks = truncate_l3(asks, l3_depth)
            truncated = len(bids) < len(book['bids']) or len(asks) < len(book['asks'])

        payload = {
            'product': product,
            'level': level,
            'captured_at': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
            'sequence': book.get('sequence'),
            'l3_depth': l3_depth if level == 3 else None,
            'truncated': truncated,
            'bids': bids,
            'asks': asks,
        }
        path = DATA_DIR / f'{name}.json.gz'
        with gzip.open(path, 'wt', encoding='utf-8') as f:
            json.dump(payload, f, separators=(',', ':'))
        print(f'{path.name}: {len(bids):,} bids / {len(asks):,} asks'
              f' ({path.stat().st_size / 1024:.0f} KiB compressed)')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[1])
    parser.add_argument('--product', default='BTC-USD')
    parser.add_argument('--l3-depth', type=int, default=None, help='truncate the L3 snapshot to this many price levels per side (default: keep all)')
    args = parser.parse_args()
    capture(args.product, args.l3_depth)
