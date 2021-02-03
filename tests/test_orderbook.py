'''
Copyright (C) 2020-2021  Bryant Moscon - bmoscon@gmail.com

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


def test_orderbook_init():
    with pytest.raises(TypeError):
        OrderBook('a')

    with pytest.raises(TypeError):
        OrderBook(blah=3)

    with pytest.raises(TypeError):
        OrderBook(max_depth='a')


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


def test_checksum():
    ob = OrderBook(checksum_format='KRAKEN')
    asks = [
    [ "0.05005", "0.00000500", "1582905487.684110" ],
    [ "0.05010", "0.00000500", "1582905486.187983" ],
    [ "0.05015", "0.00000500", "1582905484.480241" ],
    [ "0.05020", "0.00000500", "1582905486.645658" ],
    [ "0.05025", "0.00000500", "1582905486.859009" ],
    [ "0.05030", "0.00000500", "1582905488.601486" ],
    [ "0.05035", "0.00000500", "1582905488.357312" ],
    [ "0.05040", "0.00000500", "1582905488.785484" ],
    [ "0.05045", "0.00000500", "1582905485.302661" ],
    [ "0.05050", "0.00000500", "1582905486.157467" ]]
    bids = [
    [ "0.05000", "0.00000500", "1582905487.439814" ],
    [ "0.04995", "0.00000500", "1582905485.119396" ],
    [ "0.04990", "0.00000500", "1582905486.432052" ],
    [ "0.04980", "0.00000500", "1582905480.609351" ],
    [ "0.04975", "0.00000500", "1582905476.793880" ],
    [ "0.04970", "0.00000500", "1582905486.767461" ],
    [ "0.04965", "0.00000500", "1582905481.767528" ],
    [ "0.04960", "0.00000500", "1582905487.378907" ],
    [ "0.04955", "0.00000500", "1582905483.626664" ],
    [ "0.04950", "0.00000500", "1582905488.509872" ]]

    for a in asks:
        ob.asks[Decimal(a[0])] = Decimal(a[1])

    for b in bids:
        ob.bids[Decimal(b[0])] = Decimal(b[1])

    assert ob.checksum() == 974947235