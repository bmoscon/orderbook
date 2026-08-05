# Orderbook

[![License](https://img.shields.io/badge/license-GPLv3-blue.svg)](LICENSE)
![Python](https://img.shields.io/badge/Python-3.12+-green.svg)
[![PyPi](https://img.shields.io/badge/PyPi-order--book-brightgreen)](https://pypi.python.org/pypi/order-book)
![coverage-lines](https://img.shields.io/badge/coverage%3A%20lines-81.4%25-blue)
![coverage-functions](https://img.shields.io/badge/coverage%3A%20functions-100%25-blue)


A ***fast*** L2/L3 orderbook data structure, in C, for Python


### Installation

Python 3.12+ supported. In general, [uv](https://docs.astral.sh/uv/) is preferred and will be utilized throughout this document.

To add it to a project `uv add order-book` or, to install it into an environment directly, `uv pip install order-book`

Installing from a checkout of this repository: `uv pip install .` (note a C compiler is required).


### Basic Usage

```python
from decimal import Decimal

import requests
from order_book import OrderBook

ob = OrderBook()

# get some orderbook data
data = requests.get("https://api.exchange.coinbase.com/products/BTC-USD/book?level=2").json()

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

# Negative indexes work as well, so the worst bid/ask is index -1
print(f"Worst bid: {ob.bids.index(-1)}")

# Data is accessible via iteration
# Note: bids/asks are iterators

print("Top 10 bids")
for count, price in enumerate(ob.bids):
    if count == 10:
        break
    print(f"Price: {price} Size: {ob.bids[price]}")


print("\n\nTop 10 asks")
for count, price in enumerate(ob.asks):
    if count == 10:
        break
    print(f"Price: {price} Size: {ob.asks[price]}")


# Membership tests and len() work as expected
print(f"\nBest bid still in book: {ob.bids.index(0)[0] in ob.bids}")
print(f"Bid levels: {len(ob.bids)}, ask levels: {len(ob.asks)}, both sides: {len(ob)}")


# Data can be exported to a sorted dictionary
# In Python3.7+ dictionaries remain in insertion ordering. The
# dict returned by .to_dict() has had its keys inserted in sorted order
print("\n\nTop 3 asks, as a dictionary")
print(dict(list(ob.asks.to_dict().items())[:3]))


# Data can also be exported as an ordered list
# .to_list() returns a list of (price, size) tuples
print("\nTop 5 Asks")
print(ob.asks.to_list()[:5])
print("\nTop 5 Bids")
print(ob.bids.to_list()[:5])


# .keys() returns the sorted prices as a tuple
print("\nTop 5 ask prices")
print(ob.asks.keys()[:5])


# The entire book can be exported at once. The keys are 'bid' and 'ask' (singular)
book = ob.to_dict()
print(f"\nto_dict() keys: {list(book)}")
```

Both sides accept any of `bid`, `bids`, `BID`, `BIDS` (and the `ask` equivalents), as attributes or as keys:

```python
from order_book import OrderBook

ob = OrderBook()

ob.bids[100] = "1.5"     # attribute access
ob['bids'][99] = "2.0"   # key access
ob['BID'][98] = "0.5"    # case does not matter

print(ob.bid.to_list())  # [(100, '1.5'), (99, '2.0'), (98, '0.5')]

# assigning a dict to a side replaces that side wholesale
ob.asks = {101: "1.0", 102: "3.0"}
print(ob.asks.to_list())  # [(101, '1.0'), (102, '3.0')]

# levels are removed with del
del ob.asks[101]
print(ob.asks.to_list())  # [(102, '3.0')]
```

### Max Depth

`max_depth` limits how many levels are visible. `len()`, iteration, `keys()`, `index()`, `to_dict()` and `to_list()` all respect it.

```python
from order_book import OrderBook

ob = OrderBook(max_depth=3)
ob.bids = {price: price for price in range(10)}

print(len(ob.bids))        # 3
print(ob.bids.to_list())   # [(9, 9), (8, 8), (7, 7)]
print(ob.max_depth)        # 3
```

By default the levels beyond `max_depth` are still retained internally, they are just hidden. Pass `max_depth_strict=True` to have them deleted as the book is updated, which caps memory use but means out-of-depth levels can no longer be accessed:

```python
from order_book import OrderBook

ob = OrderBook(max_depth=3, max_depth_strict=True)
for price in range(10):
    ob.bids[price] = price

print(ob.bids.to_list())   # [(9, 9), (8, 8), (7, 7)]

try:
    del ob.bids[0]         # level 0 was dropped, not merely hidden
except KeyError:
    print("level 0 is gone")
```


### Checksums

Several exchanges publish a CRC32 checksum of the top of book so clients can detect a desynchronized book. Construct the book with `checksum_format` set to the exchange, then compare `ob.checksum()` against the value the exchange sent.

Supported formats: `KRAKEN`, `OKX` (and its alias `OKCOIN`), and `BITGET`.

```python
from decimal import Decimal

from order_book import OrderBook

ob = OrderBook(checksum_format='KRAKEN')

ob.bids = {Decimal(f"{100 - i}.{i:02d}"): Decimal(f"{i + 1}.5") for i in range(10)}
ob.asks = {Decimal(f"{101 + i}.{i:02d}"): Decimal(f"{i + 1}.5") for i in range(10)}

print(ob.checksum())
```


### Type conversion

`to_dict()` on either an `OrderBook` or a `SortedDict` accepts `from_type` and `to_type` keyword arguments, which convert keys and values as the dictionary is built. `from_type` restricts the conversion to values of that type; omit it to convert everything.

```python
from order_book import OrderBook

ob = OrderBook()
ob.bids = {'1.1': 2, '3.3': 4}
ob.asks = {'5.5': 6, '7.7': 8}

print(ob.to_dict(from_type=str, to_type=float))
# {'bid': {3.3: 4, 1.1: 2}, 'ask': {5.5: 6, 7.7: 8}}
# note the bid side is in descending order, as always
```


### API Summary

`OrderBook(max_depth=0, max_depth_strict=False, checksum_format=None)`

| Member | Description |
| ------ | ----------- |
| `.bids` / `.bid` / `.asks` / `.ask` | the `SortedDict` for that side; assigning a dict replaces the side |
| `ob[key]` | same sides, by key. `bid`, `bids`, `ask`, `asks`, any case |
| `.max_depth` | the configured max depth (read only) |
| `.to_dict(from_type=None, to_type=None)` | `{'bid': {...}, 'ask': {...}}` |
| `.checksum()` | CRC32 checksum in the configured exchange's format |
| `len(ob)` | total number of levels across both sides |

`SortedDict(data=None, ordering='ASC', max_depth=0, truncate=False)`

| Member | Description |
| ------ | ----------- |
| `.keys()` | tuple of keys in sorted order |
| `.index(n)` | `(key, value)` tuple at position `n`; negative indexes supported |
| `.to_dict(from_type=None, to_type=None)` | dict with keys inserted in sorted order |
| `.to_list()` | list of `(key, value)` tuples in sorted order |
| `.truncate()` | drop everything past `max_depth` |
| `sd[key]`, `sd[key] = v`, `del sd[key]`, `key in sd`, `len(sd)`, iteration | as expected; iteration yields keys in sorted order |


### Main Features

* Sides maintained in correct order
* Can perform orderbook checksums
* Supports max depth and depth truncation



### Running code coverage

The script `coverage.sh` will compile the source using the `-coverage` `CFLAG`, run the unit tests, and build a coverage report in HTML. It manages its own environment via uv, so it can be run directly. 

Note that it rebuilds `.venv` with an instrumented, unoptimized-for-timing build, so re-run `uv pip install ".[tests]"` afterwards to get back to a normal development environment.



### Running the performance tests

You can run the performance tests like so: `uv run perf/performance_test.py`. The program will profile the time to run for random data samples of various sizes as well as the construction of a sorted orderbook using live L2 orderbook data from Coinbase.

The performance of constructing a sorted orderbook (using live data from Coinbase) using this C library, versus a pure Python sorted dictionary library:


| Library        | Time, in seconds |
| ---------------| ---------------- |
| C Library      | 0.01547479629517 |
| Python Library | 0.02890801429749 |

The performance of constructing sorted dictionaries using the same libraries, as well as the cost of building unsorted, python dictionaies for dictionaries of random floating point data:


| Library        | Number of Keys | Time, in seconds |
| -------------- | -------------- | ---------------- |
| C Library      |     100        | 0.00002408027649 |
| Python Library |     100        | 0.00004816055298 |
| Python Dict    |     100        | 0.00002312660217 |
| C Library      |     500        | 0.00014019012451 |
| Python Library |     500        | 0.00027227401733 |
| Python Dict    |     500        | 0.00012207031250 |
| C Library      |     1000       | 0.00029301643372 |
| Python Library |     1000       | 0.00055193901062 |
| Python Dict    |     1000       | 0.00024676322937 |


This represents a roughly 2x speedup compared to a pure python implementation, and in many cases is close to the performance of an unsorted python dictionary.


For other performance metrics, run `performance_test.py` as well as the other performance tests in [`perf/`](perf/)
