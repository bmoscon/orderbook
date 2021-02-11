# Orderbook

[![License](https://img.shields.io/badge/license-GLPv3-blue.svg)](LICENSE)
![Python](https://img.shields.io/badge/Python-3.7+-green.svg)
[![PyPi](https://img.shields.io/badge/PyPi-order--book-brightgreen)](https://pypi.python.org/pypi/order-book)
![coverage-lines](https://img.shields.io/badge/coverage%3A%20lines-84.6%25-blue)
![coverage-functions](https://img.shields.io/badge/coverage%3A%20functions-100%25-blue)


A ***fast*** L2/L3 orderbook implementation, in C, for Python


### Basic Usage

```python
from decimal import Decimal

import requests
from order_book import OrderBook

ob = OrderBook()

# get some orderbook data
data = requests.get("https://api.pro.coinbase.com/products/BTC-USD/book?level=2").json()

ob.bids = {Decimal(price): size for price, size, _ in data['bids']}
ob.asks = {Decimal(price): size for price, size, _ in data['asks']}

# OR

for side in data:
    # there is additional data we need to ignore
    if side in {'bids', 'asks'}:
        ob[side] = {Decimal(price): size for price, size, _ in data[side]}


# Data is accessible by .index(), which returns a tuple of (price, size) at that level in the book
price, size = ob.bids.index(0)
print(f"Best bid price: {price} size: {size}")

price, size = ob.asks.index(0)
print(f"Best ask price: {price} size: {size}")

print(f"The spread is {ob.asks.index(0)[0] - ob.bids.index(0)[0]}\n\n")

# Data is accessible via iteration

print("Bids")
for price in ob.bids:
    print(f"Price: {price} Size: {ob.bids[price]}")


print("\n\nAsks")
for price in ob.asks:
    print(f"Price: {price} Size: {ob.asks[price]}")


# Data can be exported to a sorted dictionary
# in Python3.7+ dictionaries remain in insertion ordering, the
# dict returned by .to_dict() has had its keys inserted in sorted order
print("\n\nRaw asks dictionary")
print(ob.asks.to_dict())

```

### Main Features

* Sides maintained in correct order
* Can perform orderbook checksums
* Supports max depth and depth truncation


### Installation

The preferable way to install is via `pip` - `pip install order-book`. Installing from source will require a compiler and can be done with setuptools: `python setup.py install`. 


### Running code coverage

The script `coverage.sh` will compile the source using the `-coverage` `CFLAG`, run the unit tests, and build a coverage report in HTML. The script uses tools that may need to be installed (coverage, lcov, genhtml).


### Running the performance tests

You can run the performance tests like so: `python perf/performance_test.py`. The program will profile the time to run for random data samples of various sizes as well as the construction of a sorted orderbook using live L2 orderbook data from Coinbase.

The performance of constructing a sorted orderbook (using live data from Coinbase) using this C library, versus a pure Python sorted dictionary library:


| Library        | Time, in seconds |
| ---------------| ---------------- |
| C Library      | 0.00021767616271 |
| Python Library | 0.00043988227844 |

The performance of constructing sorted dictionaries using the same libraries, as well as the cost of building unsorted, python dictionaies for dictionaries of random floating point data:


| Library        | Number of Keys | Time, in seconds |
| -------------- | -------------- | ---------------- |
| C Library      |     100        | 0.00021600723266 |
| Python Library |     100        | 0.00044703483581 |
| Python Dict    |     100        | 0.00022006034851 |
| C Library      |     500        | 0.00103306770324 |
| Python Library |     500        | 0.00222206115722 |
| Python Dict    |     500        | 0.00097918510437 |
| C Library      |     1000       | 0.00202703475952 |
| Python Library |     1000       | 0.00423812866210 |
| Python Dict    |     1000       | 0.00176715850830 |


This represents a roughly 2x speedup compared to a pure python implementation, and in many cases is close to the performance of an unsorted python dictionary.


For other performance metrics, run `performance_test.py` as well as the other performance tests in [`perf/`](perf/)
