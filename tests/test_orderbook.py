'''
Copyright (C) 2020  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.
'''
from decimal import Decimal

import requests

from order_book import OrderBook


def populate_orderbook():
    ob = OrderBook()

    data = requests.get("https://api-public.sandbox.pro.coinbase.com/products/BTC-USD/book?level=2").json()
    for side, d in data.items():
        if side == 'bids':
            for price, size, _ in d:
                ob.bids[Decimal(price)] = size
        elif side == 'asks':
            for price, size, _ in d:
                ob.asks[Decimal(price)] = size

    assert ob.bids.index(0)[0] < ob.asks.index(0)[0]
    assert ob.bids.index(-1)[0] < ob.asks.index(-1)[0]

    assert ob.bids.index(-1)[0] < ob.bids.index(0)[0]
    assert ob.asks.index(-1)[0] > ob.asks.index(0)[0]

    return ob


def test_orderbook():
    populate_orderbook()


def test_to_dict():
    ob = populate_orderbook()

    data = ob.to_dict()

    assert list(data['bid'].keys()) == list(ob.bids.keys())
    assert list(data['ask'].keys()) == list(ob.asks.keys())


def test_orderbook_getitem():
    ob = OrderBook()

    data = requests.get("https://api-public.sandbox.pro.coinbase.com/products/BTC-USD/book?level=2").json()
    for side, d in data.items():
        if side in {'bids', 'asks'}:
            for price, size, _ in d:
                ob[side][Decimal(price)] = size

    assert ob.bids.index(0)[0] < ob.asks.index(0)[0]
    assert ob.bids.index(-1)[0] < ob.asks.index(-1)[0]

    assert ob.bids.index(-1)[0] < ob.bids.index(0)[0]
    assert ob.asks.index(-1)[0] > ob.asks.index(0)[0]
