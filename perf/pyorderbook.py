'''
Copyright (C) 2020-2026  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.


A pure Python order book, functionally equivalent to the C extension for performance testing
'''


class SortedDict:
    def __init__(self, ordering='DESC'):
        if ordering not in {'ASC', 'DESC'}:
            raise ValueError('Ordering must be one of ASC or DESC')
        self._ordering = ordering
        self._data = {}
        self._keys = None

    def _sorted_keys(self):
        if self._keys is None:
            self._keys = sorted(self._data, reverse=self._ordering == 'DESC')
        return self._keys

    def __setitem__(self, key, value):
        if key not in self._data:
            self._keys = None
        self._data[key] = value

    def __getitem__(self, key):
        return self._data[key]

    def __delitem__(self, key):
        del self._data[key]
        self._keys = None

    def __contains__(self, key):
        return key in self._data

    def __len__(self):
        return len(self._data)

    def __iter__(self):
        return iter(self._sorted_keys())

    def keys(self):
        return list(self._sorted_keys())

    def index(self, idx):
        key = self._sorted_keys()[idx]
        return key, self._data[key]

    def to_dict(self):
        return {key: self._data[key] for key in self._sorted_keys()}


class OrderBook:
    def __init__(self):
        self.bids = SortedDict(ordering='DESC')
        self.asks = SortedDict(ordering='ASC')

    def __getitem__(self, key):
        if key in {'bid', 'bids', 'BID', 'BIDS'}:
            return self.bids
        if key in {'ask', 'asks', 'ASK', 'ASKS'}:
            return self.asks
        raise KeyError(key)

    def to_dict(self):
        return {'bid': self.bids.to_dict(), 'ask': self.asks.to_dict()}
