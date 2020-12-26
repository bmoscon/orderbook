from decimal import Decimal
from functools import wraps
import random
import resource
import time

from sortedcontainers import SortedDict as sd
import requests

from orderbook import SortedDict, OrderBook


data = requests.get("https://api-public.sandbox.pro.coinbase.com/products/BTC-USD/book?level=2").json()


def profile(mem_usage):
    def _profile(f):
        @wraps(f)
        def wrapper(*args, **kwargs):
            startm = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
            startt = time.time()
            ret = f(*args, **kwargs)
            total_time = time.time() - startt
            mem = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss - startm
            print("Time:", total_time)
            if mem_usage:
                print("Mem usage:", mem)
            return ret
        return wrapper
    return _profile


@profile(mem_usage=False)
def profile_orderbook():
    ob = OrderBook()

    for side, d in data.items():
        if side == 'bids':
            for price, size, _ in d:
                ob.bids[Decimal(price)] = size
        elif side == 'asks':
            for price, size, _ in d:
                ob.asks[Decimal(price)] = size
    ob.to_dict()


@profile(mem_usage=False)
def profile_orderbook_sd():
    ob = {'bid': sd(), 'ask': sd()}

    for side, d in data.items():
        if side == 'bids':
            for price, size, _ in d:
                ob['bid'][Decimal(price)] = size
        elif side == 'asks':
            for price, size, _ in d:
                ob['ask'][Decimal(price)] = size


def random_data_test(size):
    random.seed()
    values = []
    asc = SortedDict(ordering='ASC')
    sorteddict = sd()
    raw_python = {}

    while len(values) != size:
        for _ in range(size):
            values.append(random.uniform(-100000.0, 100000.0))
        values = set(values)

    @profile(mem_usage=True)
    def test_ordered(dictionary):
        for v in values:
            dictionary[v] = str(v)

        previous = None
        for key in dictionary:
            assert key in values
            assert str(key) == dictionary[key]
            if previous:
                assert previous < key
            previous = key

    @profile(mem_usage=True)
    def test_unordered(unordered):
        for v in values:
            unordered[v] = str(v)

        previous = None
        for key in unordered:
            assert key in values
            assert str(key) == unordered[key]
            if previous:
                assert previous != key
            previous = key

    print(f"C lib with {size} entries")
    test_ordered(asc)
    print(f"SortedDict Python lib with {size} entries")
    test_ordered(sorteddict)
    print(f"Python dict (non sorted) with {size} entries")
    test_unordered(raw_python)


def random_data_performance():
    for size in (10, 100, 200, 400, 500, 1000, 2000, 10000, 100000, 200000, 500000):
        random_data_test(size)


if __name__ == "__main__":
    print("Sorted Dict Performance\n")
    random_data_performance()
    print("\n\nOrderbook Overall\n")
    print("C lib OrderBook")
    profile_orderbook()
    print("Python lib OrderBook")
    profile_orderbook_sd()
