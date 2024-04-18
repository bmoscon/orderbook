'''
Copyright (C) 2020-2024  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.
'''
from decimal import Decimal
from functools import wraps
import random
import time

from sortedcontainers import SortedDict as sd
import requests

from order_book import SortedDict, OrderBook
from pyorderbook import OrderBook as PythonOrderbook, SortedDict as PythonSortedDict


data = requests.get("https://api.pro.coinbase.com/products/BTC-USD/book?level=2").json()


def profile(f):
    @wraps(f)
    def wrapper(*args, **kwargs):
        startt = time.time()
        ret = f(*args, **kwargs)
        total_time = time.time() - startt
        print(f"Time: {total_time * 1000:.6f} ms")
        return ret
    return wrapper


@profile
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


@profile
def profile_orderbook_sd():
    ob = {'bid': sd(), 'ask': sd()}

    for side, d in data.items():
        if side == 'bids':
            for price, size, _ in d:
                ob['bid'][Decimal(price)] = size
        elif side == 'asks':
            for price, size, _ in d:
                ob['ask'][Decimal(price)] = size


@profile
def profile_orderbook_python():
    ob = PythonOrderbook()

    for side, d in data.items():
        if side == 'bids':
            for price, size, _ in d:
                ob['bid'][Decimal(price)] = size
        elif side == 'asks':
            for price, size, _ in d:
                ob['ask'][Decimal(price)] = size

    ob.to_dict()


def random_data_test(size):
    random.seed()
    values = []
    asc = SortedDict(ordering='ASC')
    sorteddict = sd()
    raw_python = {}
    python_sd = PythonSortedDict(ordering='ASC')

    while len(values) != size:
        for _ in range(size):
            values.append(random.uniform(-100000.0, 100000.0))
        values = set(values)

    print("===== Write Performance =====")

    @profile
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

    @profile
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
    print(f"Orderbook SortedDict Python lib with {size} entries")
    test_ordered(python_sd)
    print(f"Python dict (non sorted) with {size} entries")
    test_unordered(raw_python)

    print("===== Read Performance =====")
    @profile
    def access_top10_c_index(c_dictionary):
        for idx in range(10):
            a = c_dictionary.index(idx)

    @profile
    def access_top10_c_keys(c_dictionary):
        for idx, key in enumerate(c_dictionary.keys()):
            if idx >= 10:
                break
            a = c_dictionary[key]

    @profile
    def access_top10_c_tolist(c_dictionary):
        a = c_dictionary.to_list()

    @profile
    def access_top10_c_todict(c_dictionary):
        d = c_dictionary.to_dict()
        for idx, key in enumerate(d):
            a = d[key]
            if idx >= 10:
                break

    @profile
    def access_top10_iter(dictionary):
        for idx, key in enumerate(dictionary):
            a = dictionary[key]
            if idx >= 10:
                break

    print(f"C lib with {size} entries (access)")
    
    print(f"- index impl ", end="")
    access_top10_c_index(asc)
    
    print(f"- todict impl ", end="")
    access_top10_c_todict(asc)

    print(f"- keys impl ", end="")
    access_top10_c_keys(asc)

    print(f"- iter impl (incorrect when called multiple times) ", end="")
    access_top10_iter(asc)

    print(f"- tolist impl ", end="")
    access_top10_c_tolist(asc)
    
    print(f"Orderbook SortedDict Python lib with {size} entries (access)")
    access_top10_iter(python_sd)
    
    print(f"Python dict (non sorted) with {size} entries (access)")
    access_top10_iter(raw_python)



def random_data_performance():
    for size in (10, 100, 200, 400, 500, 1000, 2000, 10000, 100000, 200000, 500000):
        random_data_test(size)


if __name__ == "__main__":
    print("Sorted Dict Performance\n")
    random_data_performance()

    print("\n\nOrderbook Overall\n")
    print("C lib OrderBook")
    profile_orderbook()
    print("Sortedcontainers OrderBook")
    profile_orderbook_sd()
    print("Python OrderBook")
    profile_orderbook_python()
