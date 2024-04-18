'''
Copyright (C) 2020-2024  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.
'''
from decimal import Decimal
import random

import pytest
import requests

from order_book import OrderBook


def populate_orderbook():
    ob = OrderBook()

    data = requests.get("https://api.pro.coinbase.com/products/BTC-USD/book?level=2").json()
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

    data = requests.get("https://api.pro.coinbase.com/products/BTC-USD/book?level=2").json()
    for side, d in data.items():
        if side in {'bids', 'asks'}:
            for price, size, _ in d:
                ob[side][Decimal(price)] = size

    assert ob.bids.index(0)[0] < ob.asks.index(0)[0]
    assert ob.bids.index(-1)[0] < ob.asks.index(-1)[0]

    assert ob.bids.index(-1)[0] < ob.bids.index(0)[0]
    assert ob.asks.index(-1)[0] > ob.asks.index(0)[0]

    with pytest.raises(KeyError):
        # legal keys are BID, bid, BIDS, bids, ASK, ask, ASKS, asks
        ob['invalid'][1] = 3


def test_orderbook_init_errors():
    with pytest.raises(TypeError):
        OrderBook('a')

    with pytest.raises(TypeError):
        OrderBook(blah=3)

    with pytest.raises(TypeError):
        OrderBook(max_depth='a')


def test_orderbook_init():
    ob = OrderBook(max_depth=10)
    ob.bids = {val: val for val in range(20)}
    ob.asks = {val: val for val in range(10, 30)}
    assert len(ob) == 20


def test_orderbook_len():
    random.seed()
    bids = []
    asks = []

    for _ in range(500):
        bids.append(random.uniform(0.0, 100000.0))
    bids = list(set(bids))

    for _ in range(500):
        asks.append(random.uniform(0.0, 100000.0))
    asks = list(set(asks))

    ob = OrderBook()

    for b in bids:
        ob['BIDS'][b] = str(b)
    for a in asks:
        ob['ASKS'][a] = str(a)

    assert len(ob) == len(asks) + len(bids)


def test_orderbook_keys():
    ob = OrderBook()

    ob['bids'][1] = 1
    ob['BIDS'][1] = 2
    ob['bid'][1] = 3
    ob['BID'][1] = 4

    assert ob.bids.to_dict() == {1: 4}
    assert ob.bid.to_dict() == {1: 4}

    ob['asks'][1] = 1
    ob['ASKS'][1] = 2
    ob['ask'][1] = 3
    ob['ASK'][1] = 4

    assert ob.asks.to_dict() == {1: 4}
    assert ob.ask.to_dict() == {1: 4}


def test_orderbook_setitem():
    ob = OrderBook()

    data = requests.get("https://api.pro.coinbase.com/products/BTC-USD/book?level=2").json()
    ob.bids = {Decimal(price): size for price, size, _ in data['bids']}
    ob.asks = {Decimal(price): size for price, size, _ in data['asks']}

    assert ob.bids.index(0)[0] < ob.asks.index(0)[0]
    assert ob.bids.index(-1)[0] < ob.asks.index(-1)[0]

    assert ob.bids.index(-1)[0] < ob.bids.index(0)[0]
    assert ob.asks.index(-1)[0] > ob.asks.index(0)[0]


def test_orderbook_getitem_invalid():
    ob = OrderBook()

    with pytest.raises(ValueError):
        ob[1][1] = 'a'


def test_orderbook_setitem_invalid():
    ob = OrderBook()

    with pytest.raises(ValueError):
        ob[123] = {}

    with pytest.raises(ValueError):
        ob['invalid'] = {}

    with pytest.raises(ValueError):
        del ob['bids']

    with pytest.raises(ValueError):
        ob['bids'] = 'a'


def test_checksum_raises():
    with pytest.raises(ValueError):
        ob = OrderBook()
        ob.checksum()


def test_l3_orderbook():
    ob = OrderBook()
    raw_dict = {}

    data = requests.get("https://api.pro.coinbase.com/products/BTC-USD/book?level=3").json()
    for side, d in data.items():
        if side in {'bids', 'asks'}:
            raw_dict[side] = {}
            for price, size, orderid in d:
                p = Decimal(price)
                if p in ob[side]:
                    ob[side][p][orderid] = size
                    raw_dict[side][p][orderid] = size
                    assert p in ob[side]
                    assert len(ob[side][p]) > 1
                else:
                    ob[side][p] = {orderid: size}
                    raw_dict[side][p] = {orderid: size}
                    assert p in ob[side]
                    assert len(ob[side][p]) == 1

    assert ob.bids.index(0)[0] < ob.asks.index(0)[0]
    assert ob.bids.index(-1)[0] < ob.asks.index(-1)[0]

    assert ob.bids.index(-1)[0] < ob.bids.index(0)[0]
    assert ob.asks.index(-1)[0] > ob.asks.index(0)[0]

    ob_dict = ob.to_dict()
    for side in raw_dict:
        assert raw_dict[side] == ob[side].to_dict() == ob_dict[side[:-1]]

    # test to_dict with type conversion
    def convert(x):
        if isinstance(x, Decimal):
            return float(x)
        if isinstance(x, dict):
            return {k: float(v) for k, v in x.items()}
    print("DOING")
    ob_dict = ob.to_dict(to_type=convert)
    for price in ob_dict['bid']:
        assert isinstance(price, float)
        for value in list(ob_dict['bid'][price].values()):
            assert isinstance(value, float)


def test_del():
    ob = OrderBook()

    ob.bids = {1: 1, 2: 2}
    ob.asks = {5: 5, 6: 6, 7: 7}

    assert len(ob.bids) == 2

    del ob.bids[1]

    assert 1 not in ob.bids
    assert 2 in ob.bids
    assert len(ob.bids) == 1
    assert ob.bids.to_dict() == {2: 2}


def test_to_dict_types():
    input = {
        '1.1': 2,
        '3.3': 4,
        '5.5': 6,
        '7.7': 8
    }
    ob = OrderBook()
    ob.bids = input
    ob.asks = input

    data = ob.to_dict(from_type=str, to_type=float)
    assert data['bid'] == {1.1: 2, 3.3: 4, 5.5: 6, 7.7: 8}
    assert data['ask'] == {1.1: 2, 3.3: 4, 5.5: 6, 7.7: 8}
