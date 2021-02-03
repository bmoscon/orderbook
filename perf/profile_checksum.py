from decimal import Decimal
import time
import zlib

from sortedcontainers import SortedDict as sd
from order_book import OrderBook
import requests


r = requests.get("https://api.kraken.com/0/public/Depth?pair=XETHZUSD&count=1000")
r.raise_for_status()
data = r.json()


BID = 'bid'
ASK = 'ask'


def calc_checksum(book):
    bid_prices = list(reversed(book[BID].keys()))[:10]
    ask_prices = list(book[ASK].keys())[:10]

    combined = ""
    for d, side in ((ask_prices, ASK), (bid_prices, BID)):
        sizes = [str(book[side][price]).replace('.', '').lstrip('0') for price in d]
        prices = [str(price).replace('.', '').lstrip('0') for price in d]
        combined += ''.join([b for a in zip(prices, sizes) for b in a])

    return str(zlib.crc32(combined.encode()))


def main():
    ob = OrderBook(checksum_format='KRAKEN')

    asks = {
        Decimal("0.05005"): Decimal("0.00000500"),
        Decimal("0.05010"): Decimal("0.00000500"),
        Decimal("0.05015"): Decimal("0.00000500"),
        Decimal("0.05020"): Decimal("0.00000500"),
        Decimal("0.05025"): Decimal("0.00000500"),
        Decimal("0.05030"): Decimal("0.00000500"),
        Decimal("0.05035"): Decimal("0.00000500"),
        Decimal("0.05040"): Decimal("0.00000500"),
        Decimal("0.05045"): Decimal("0.00000500"),
        Decimal("0.05050"): Decimal("0.00000500")
    }

    bids = {
        Decimal("0.05000"): Decimal("0.00000500"),
        Decimal("0.04995"): Decimal("0.00000500"),
        Decimal("0.04990"): Decimal("0.00000500"),
        Decimal("0.04980"): Decimal("0.00000500"),
        Decimal("0.04975"): Decimal("0.00000500"),
        Decimal("0.04970"): Decimal("0.00000500"),
        Decimal("0.04965"): Decimal("0.00000500"),
        Decimal("0.04960"): Decimal("0.00000500"),
        Decimal("0.04955"): Decimal("0.00000500"),
        Decimal("0.04950"): Decimal("0.00000500")
    }

    ob.asks = asks
    ob.bids = bids
    book = {BID: sd(bids), ASK: sd(asks)}

    start = time.time()
    checksum = calc_checksum(book)
    end = time.time()
    assert checksum == '974947235'

    print("Depth 10 Example")
    print(f"Python: {(end - start) * 1000000} microseconds")

    start = time.time()
    checksum = ob.checksum()
    end = time.time()

    assert checksum == 974947235

    print(f"C: {(end - start) * 1000000} microseconds")

    print("\nDepth 1000 Example")

    book = {BID: sd({Decimal(update[0]): Decimal(update[1]) for update in data['result']['XETHZUSD']['bids']}),
            ASK: sd({Decimal(update[0]): Decimal(update[1]) for update in data['result']['XETHZUSD']['asks']})
            }

    ob.bids = {Decimal(update[0]): Decimal(update[1]) for update in data['result']['XETHZUSD']['bids']}
    ob.asks = {Decimal(update[0]): Decimal(update[1]) for update in data['result']['XETHZUSD']['asks']}

    start = time.time()
    checksum1 = calc_checksum(book)
    end = time.time()

    print(f"Python: {(end - start) * 1000000} microseconds")

    start = time.time()
    checksum2 = ob.checksum()
    end = time.time()

    print(f"C: {(end - start) * 1000000} microseconds")

    assert checksum1 == str(checksum2)


if __name__ == '__main__':
    main()
