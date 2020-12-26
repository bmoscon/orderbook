from decimal import Decimal
from functools import wraps
import random
import resource
import time

from sortedcontainers import SortedDict as sd
from orderbook import SortedDict


def profile(f):
    @wraps(f)
    def wrapper(*args, **kwargs):
        startm = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
        startt = time.time()
        ret = f(*args, **kwargs)
        total_time = time.time() - startt
        mem = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss - startm
        print("Time:", total_time)
        print("Mem usage:", mem)
        return ret
    return wrapper


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

    @profile
    def test_c():
        for v in values:
            asc[v] = str(v)

        previous = None
        for key in asc:
            assert key in values
            assert str(key) == asc[key]
            if previous:
                assert previous < key
            previous = key
    
    @profile
    def test_py():
        for v in values:
            sorteddict[v] = str(v)

        previous = None
        for key in sorteddict:
            assert key in values
            assert str(key) == sorteddict[key]
            if previous:
                assert previous < key
            previous = key
    
    @profile
    def test_raw_py():
        for v in values:
            raw_python[v] = str(v)

        previous = None
        for key in raw_python:
            assert key in values
            assert str(key) == sorteddict[key]
            if previous:
                assert previous != key
            previous = key

    
    print(f"C test with {size} entries")
    test_c()
    print(f"Python lib testwith {size} entries")
    test_py()
    print(f"Python dict testwith {size} entries")
    test_raw_py()


def random_data_performance():
    for size in (10, 100, 200, 400, 500, 1000, 2000, 10000, 100000, 200000, 500000):
        random_data_test(size)

if __name__ == "__main__":
    random_data_performance()

    