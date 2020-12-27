# Orderbook

[![License](https://img.shields.io/badge/license-GLPv3-blue.svg)](LICENSE)
![Python](https://img.shields.io/badge/Python-3.7+-green.svg)
[![PyPi](https://img.shields.io/badge/PyPi-order--book-brightgreen)](https://pypi.python.org/pypi/order-book)

A ***fast*** orderbook implementation, in C, for Python

### Installation

The preferable way to install is via `pip` - `pip install order-book`. Installing from source will require a compiler and can be done with setuptools: `python setup.py install`. 


### Running code coverage

The script `coverage.sh` will compile the source using the `-coverage` `CFLAG`, run the unit tests, and build a coverage report in HTML. The script contains some dependencies that may need to be installed (coverage, lcov, genhtml).


### Running the performance tests

You can run the performance tests like so: `python perf/performance_test.py`. The program will profile the time to run for random data samples of various sizes as well as the construction of a sorted orderbook using live L2 orderbook data from Coinbase. 
