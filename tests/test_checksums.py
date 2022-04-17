
'''
Copyright (C) 2020-2021  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.
'''
from decimal import Decimal
import json
import zlib

import pytest
import requests
from sortedcontainers import SortedDict as sd

from order_book import OrderBook


def test_minimum_depth_kraken():
    ob = OrderBook(max_depth=9, checksum_format='KRAKEN')
    with pytest.raises(ValueError):
        ob.checksum()


def test_kraken_checksum():
    # This checksum is from the kraken docs
    ob = OrderBook(max_depth=10, checksum_format='KRAKEN')
    asks = [
        ["0.05005", "0.00000500", "1582905487.684110"],
        ["0.05010", "0.00000500", "1582905486.187983"],
        ["0.05015", "0.00000500", "1582905484.480241"],
        ["0.05020", "0.00000500", "1582905486.645658"],
        ["0.05025", "0.00000500", "1582905486.859009"],
        ["0.05030", "0.00000500", "1582905488.601486"],
        ["0.05035", "0.00000500", "1582905488.357312"],
        ["0.05040", "0.00000500", "1582905488.785484"],
        ["0.05045", "0.00000500", "1582905485.302661"],
        ["0.05050", "0.00000500", "1582905486.157467"]
    ]

    bids = [
        ["0.05000", "0.00000500", "1582905487.439814"],
        ["0.04995", "0.00000500", "1582905485.119396"],
        ["0.04990", "0.00000500", "1582905486.432052"],
        ["0.04980", "0.00000500", "1582905480.609351"],
        ["0.04975", "0.00000500", "1582905476.793880"],
        ["0.04970", "0.00000500", "1582905486.767461"],
        ["0.04965", "0.00000500", "1582905481.767528"],
        ["0.04960", "0.00000500", "1582905487.378907"],
        ["0.04955", "0.00000500", "1582905483.626664"],
        ["0.04950", "0.00000500", "1582905488.509872"]
    ]

    for a in asks:
        ob.asks[Decimal(a[0])] = Decimal(a[1])

    for b in bids:
        ob.bids[Decimal(b[0])] = Decimal(b[1])

    assert ob.checksum() == 974947235


def test_okx_checksum():
    ob = OrderBook(checksum_format='OKX')

    asks = {Decimal("3366.8"): Decimal("9"), Decimal("3368"): Decimal("8"), Decimal("3372"): Decimal("8")}
    bids = {Decimal("3366.1"): Decimal("7")}
    expected = 831078360

    ob.bids = bids
    ob.asks = asks

    assert ob.checksum() == expected


def test_okcoin_checksum():
    ob = OrderBook(checksum_format='OKCOIN')

    asks = {Decimal("3366.8"): Decimal("9"), Decimal("3368"): Decimal("8"), Decimal("3372"): Decimal("8")}
    bids = {Decimal("3366.1"): Decimal("7")}
    expected = 831078360

    ob.bids = bids
    ob.asks = asks

    assert ob.checksum() == expected


def test_ftx_checksum():
    BID = 'bid'
    ASK = 'ask'

    r = requests.get("https://ftx.com/api/markets/BTC-PERP/orderbook?depth=100")
    r.raise_for_status()
    ftx_data = json.loads(r.text, parse_float=Decimal)

    def ftx_checksum(book):
        bid_it = reversed(book[BID])
        ask_it = iter(book[ASK])

        bids = [f"{bid}:{book[BID][bid]}" for bid in bid_it]
        asks = [f"{ask}:{book[ASK][ask]}" for ask in ask_it]

        if len(bids) == len(asks):
            combined = [val for pair in zip(bids, asks) for val in pair]
        elif len(bids) > len(asks):
            combined = [val for pair in zip(bids[:len(asks)], asks) for val in pair]
            combined += bids[len(asks):]
        else:
            combined = [val for pair in zip(bids, asks[:len(bids)]) for val in pair]
            combined += asks[len(bids):]

        computed = ":".join(combined).encode()
        return zlib.crc32(computed)

    ob = OrderBook(checksum_format='FTX')

    book = {BID: sd({Decimal(update[0]): Decimal(update[1]) for update in ftx_data['result']['bids']}),
            ASK: sd({Decimal(update[0]): Decimal(update[1]) for update in ftx_data['result']['asks']})
            }
    ob.bids = {Decimal(update[0]): Decimal(update[1]) for update in ftx_data['result']['bids']}
    ob.asks = {Decimal(update[0]): Decimal(update[1]) for update in ftx_data['result']['asks']}

    ob.checksum() == ftx_checksum(book)
