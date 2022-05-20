
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

    # The following checksums are from recorded data
    ob = OrderBook(max_depth=11, checksum_format='KRAKEN')

    asks = [
        ["0.620000", "40.00000000"],
        ["0.830000", "380.86649128"],
        ["1.500000", "333.33333333"]
    ]

    bids = [
        ["0.520300", "3943.09454867"],
        ["0.403200", "454.31671175"],
        ["0.403100", "1522.68122054"],
        ["0.403000", "43.31058726"],
        ["0.353200", "49.38467346"],
        ["0.261600", "66.67686034"],
        ["0.111000", "99.09909910"],
        ["0.110000", "909.09090909"],
        ["0.000600", "3333.33333333"],
        ["0.000400", "5000.00000000"],
        ["0.000100", "1000000.00000000"]
    ]

    for a in asks:
        ob.asks[Decimal(a[0])] = Decimal(a[1])

    for b in bids:
        ob.bids[Decimal(b[0])] = Decimal(b[1])

    assert ob.checksum() == 577149452

    ob = OrderBook(max_depth=11, checksum_format='KRAKEN')

    asks = [
        ["0.814900", "297.71298000"],
        ["0.815000", "500.00000000"],
        ["0.815100", "500.00399385"],
        ["0.815200", "42.03000000"],
        ["0.815300", "21.50000000"],
        ["0.815400", "10.75000000"],
        ["0.829900", "1442.34063708"],
        ["0.830000", "380.86649128"],
        ["1.500000", "333.33333333"]
    ]

    bids = [
        ["0.473400", "1284.67569684"],
        ["0.441000", "40.00415721"],
        ["0.342200", "51.43191116"],
        ["0.261600", "66.92596839"],
        ["0.111000", "99.09909910"],
        ["0.110000", "909.09090909"],
        ["0.000600", "3333.33333333"],
        ["0.000400", "5000.00000000"],
        ["0.000100", "1000000.00000000"]
    ]

    for a in asks:
        ob.asks[Decimal(a[0])] = Decimal(a[1])

    for b in bids:
        ob.bids[Decimal(b[0])] = Decimal(b[1])

    assert ob.checksum() == 2369158246

    ob = OrderBook(max_depth=11, checksum_format='KRAKEN')

    asks = [
        ["0.000017680", "38663.54992198"],
        ["0.000017690", "20623.74841086"],
        ["0.000017700", "103797.62636430"],
        ["0.000017710", "40745.97057228"],
        ["0.000017720", "13296.04740856"],
        ["0.000017730", "42078.86085768"],
        ["0.000017740", "64.38065876"],
        ["0.000017760", "1131.70427847"],
        ["0.000017780", "43891.46024565"],
        ["0.000017790", "43908.00000000"],
        ["0.000017810", "0.00005437"]
    ]

    bids = [
        ["0.000017670", "0.00000048"], # small volume causes scientific notation
        ["0.000017660", "50.29884341"],
        ["0.000017650", "16958.37856622"],
        ["0.000017640", "16735.08043085"],
        ["0.000017630", "61895.21671233"],
        ["0.000017620", "86958.66158205"],
        ["0.000017610", "8564.64738216"],
        ["0.000017600", "59539.93801826"],
        ["0.000017580", "52578.63046424"],
        ["0.000017570", "46812.60266777"],
        ["0.000017560", "640.09877588"]
    ]

    for a in asks:
        ob.asks[Decimal(a[0])] = Decimal(a[1])

    for b in bids:
        ob.bids[Decimal(b[0])] = Decimal(b[1])

    assert ob.checksum() == 1611253991


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
