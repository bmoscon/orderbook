
'''
Copyright (C) 2020-2021  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.
'''
from decimal import Decimal

import pytest

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
