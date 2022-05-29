'''
Copyright (C) 2020-2022  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.


This class is functionally equivalent to the C class (for the basic use cases). It is used for performance comparisons only
'''


class OrderBook:
    def __init__(self, max_depth=None, truncate=None):
        self.__bids = SortedDict(ordering='DESC')
        self.__asks = SortedDict(ordering='ASC')

    def __getitem__(self, key):
        if key in {'bid', 'bids', 'BID', 'BIDS'}:
            return self.__bids
        elif key in {'ask', 'asks', 'ASK', 'ASKS'}:
            return self.__asks

    def __getattr__(self, attr):
        if attr in {'bid', 'bids', 'BID', 'BIDS'}:
            return self.__bids
        elif attr in {'ask', 'asks', 'ASK', 'ASKS'}:
            return self.__asks

    def to_dict(self):
        ret = {}
        ret['bids'] = {bid: self.__bids[bid] for bid in self.__bids}
        ret['asks'] = {ask: self.__asks[ask] for ask in self.__asks}
        return ret


class SortedDict:
    def __init__(self, *args, ordering='DESC'):
        self.ordering = ordering
        self.data = {}
        self.position = 0
        self.__keys = None
        self.__dirty = False

        if len(args):
            if len(args > 1):
                raise ValueError('SortedDict only takes a single positional argument')
            if not isinstance(args[0], dict):
                raise ValueError('SortedDict only takes a dictionary as a positional argument')

            self.data = dict(args[0])

        if ordering not in {'ASC', 'DESC'}:
            raise ValueError('Ordering must be one of ASC or DESC')

    def __update_key_cache(self):
        if self.__dirty or self.__keys is None:
            keys = sorted(self.data.keys(), reverse=self.ordering == 'DESC')
            self.__keys = keys
            self.__dirty = False

    def keys(self):
        self.__update_key_cache()
        return list(self.__keys)

    def __setitem__(self, key, value):
        self.dirty = True
        self.data[key] = value

    def __getitem__(self, key):
        return self.data[key]

    def __delitem__(self, key):
        self.dirty = True
        del self.data[key]

    def __iter__(self):
        return self

    def __next__(self):
        self.__update_key_cache()
        while self.position < len(self.__keys):
            ret = self.__keys[self.position]
            self.position += 1
            return ret
        raise StopIteration
