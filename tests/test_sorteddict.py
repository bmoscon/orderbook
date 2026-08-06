'''
Copyright (C) 2020-2026  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.
'''
from decimal import Decimal
import random

import pytest

from order_book import SortedDict


def test_ascending():
    s = SortedDict(ordering='ASC')
    s[3] = "a"
    s[2] = "b"
    s[1] = "c"

    assert s.keys() == (1, 2, 3)


def test_descending():
    s = SortedDict(ordering='DESC')
    s[1] = "a"
    s[3] = "b"
    s[2] = "c"

    assert s.keys() == (3, 2, 1)


def test_iteration():
    expected = (3, 2, 1)
    index = None
    s = SortedDict(ordering='DESC')
    s[1] = "a"
    s[3] = "b"
    s[2] = "c"

    for index, key in enumerate(s):
        assert key == expected[index]
    assert index == 2


def test_nested_iteration():
    d = SortedDict({1: 'a', 2: 'b'})

    pairs = [(a, b) for a in d for b in d]
    assert pairs == [(1, 1), (1, 2), (2, 1), (2, 2)]

    it1 = iter(d)
    it2 = iter(d)
    assert it1 is not d
    assert it1 is not it2
    assert next(it1) == 1
    assert next(it2) == 1
    assert next(it1) == 2


def test_iteration_depth():
    d = SortedDict({i: i for i in range(100)}, max_depth=10)
    assert list(d) == list(range(10))


def test_index():
    s = SortedDict(ordering='DESC')
    s[1] = "a"
    s[3] = "b"
    s[2] = "c"

    assert s.index(0) == (3, "b")
    assert s.index(1) == (2, "c")
    assert s.index(2) == (1, "a")
    assert s.index(-1) == (1, 'a')
    assert s.index(-2) == (2, 'c')
    assert s.index(-3) == (3, 'b')

    with pytest.raises(IndexError):
        assert s.index(3) == (2, "c")

    with pytest.raises(IndexError):
        assert s.index(4) == (2, "c")


def test_decimal():
    s = SortedDict(ordering='DESC')
    s[Decimal('1.2')] = "a"
    s[Decimal('1.5')] = "b"
    s[Decimal('1.6')] = "c"
    s[Decimal('1.7')] = "d"

    assert len(s) == 4

    assert s.keys() == (Decimal('1.7'), Decimal('1.6'), Decimal('1.5'), Decimal('1.2'))


def test_random_data():
    random.seed()
    values = []
    asc = SortedDict(ordering='ASC')
    desc = SortedDict(ordering='DESC')

    for _ in range(2000):
        values.append(random.uniform(0.0, 100000.0))
    values = set(values)

    for v in values:
        asc[v] = str(v)
        desc[v] = str(v)

    previous = None
    for key in asc:
        assert key in values
        assert str(key) == asc[key]
        if previous:
            assert previous < key
        previous = key

    previous = None
    for key in desc:
        assert key in values
        assert str(key) == desc[key]
        if previous:
            assert previous > key
        previous = key


def test_to_dict():
    random.seed()
    values = []
    asc = SortedDict(ordering='ASC')
    desc = SortedDict(ordering='DESC')

    for _ in range(2000):
        values.append(random.uniform(0.0, 100000.0))
    values = set(values)

    for v in values:
        asc[v] = str(v)
        desc[v] = str(v)

    d = asc.to_dict()
    assert list(d.keys()) == list(asc.keys())
    assert sorted(d.keys()) == list(d.keys())
    previous = None
    for key, _ in d.items():
        assert d[key] == asc[key]
        if previous:
            d[key] > previous
        previous = d[key]

    d = desc.to_dict()
    assert list(d.keys()) == list(desc.keys())
    assert list(reversed(sorted(d.keys()))) == list(d.keys())
    previous = None
    for key, val in d.items():
        assert d[key] == desc[key]
        if previous:
            d[key] < previous
        previous = d[key]

def test_to_list():
    random.seed()
    values = []
    asc = SortedDict(ordering='ASC')
    desc = SortedDict(ordering='DESC')

    for _ in range(2000):
        values.append(random.uniform(0.0, 100000.0))
    values = set(values)

    for v in values:
        asc[v] = str(v)
        desc[v] = str(v)

    lst = asc.to_list()
    _keys = list(list(zip(*lst))[0])
    assert _keys == list(asc.keys())
    assert sorted(_keys) == _keys
    previous = None
    for key, val in lst:
        assert val == asc[key]
        val = float(val)
        if previous:
            assert val > previous
        previous = val

    lst = desc.to_list()
    _keys = list(list(zip(*lst))[0])
    assert _keys == list(desc.keys())
    assert list(reversed(sorted(_keys))) == _keys
    previous = None
    for key, val in lst:
        assert val == desc[key]
        val = float(val)
        if previous:
            assert val < previous
        previous = val

def test_items():
    asc = SortedDict({4: 'a', 1: 'c', 3: 'f'}, ordering='ASC')
    desc = SortedDict({4: 'a', 1: 'c', 3: 'f'}, ordering='DESC')

    assert list(asc.items()) == [(1, 'c'), (3, 'f'), (4, 'a')]
    assert list(desc.items()) == [(4, 'a'), (3, 'f'), (1, 'c')]
    assert list(asc.items()) == asc.to_list()
    assert list(desc.items()) == desc.to_list()

    for key, value in asc.items():
        assert asc[key] == value


def test_items_is_lazy():
    d = SortedDict({1: 'a', 2: 'b', 3: 'c'})
    it = d.items()
    assert iter(it) is it
    assert next(it) == (1, 'a')
    assert next(it) == (2, 'b')
    assert next(it) == (3, 'c')
    with pytest.raises(StopIteration):
        next(it)


def test_items_depth():
    d = SortedDict({i: i for i in range(100)}, max_depth=10)
    assert list(d.items()) == [(i, i) for i in range(10)]


def test_items_mutation():
    d = SortedDict({1: 'a', 2: 'b'})
    it = d.items()
    assert next(it) == (1, 'a')
    d[2] = 'z'
    assert next(it) == (2, 'z')

    # a level deleted mid iteration raises
    d = SortedDict({1: 'a', 2: 'b'})
    it = d.items()
    assert next(it) == (1, 'a')
    del d[2]
    with pytest.raises(KeyError):
        next(it)


def test_init_from_dict():
    with pytest.raises(TypeError):
        asc = SortedDict("a", ordering='ASC')

    with pytest.raises(TypeError):
        asc = SortedDict({}, {}, ordering='ASC')

    asc = SortedDict({4: 'a', 1: 'c', 3: 'f', 6: 'j', 9: 'z', 2: 'p'}, ordering='ASC')
    assert asc.to_dict() == {1: 'c', 2: 'p', 3: 'f', 4: 'a', 6: 'j', 9: 'z'}
    assert list(asc.keys()) == [1, 2, 3, 4, 6, 9]

    desc = SortedDict({4: 'a', 1: 'c', 3: 'f', 6: 'j', 9: 'z', 2: 'p'}, ordering='DESC')
    assert desc.to_dict() == {1: 'c', 2: 'p', 3: 'f', 4: 'a', 6: 'j', 9: 'z'}
    assert list(desc.keys()) == [9, 6, 4, 3, 2, 1]


def test_invalid_ordering():
    with pytest.raises(ValueError):
        SortedDict(ordering='D')

    with pytest.raises(ValueError):
        SortedDict(ordering=1)


def test_default_ordering():
    # default ordering is ascending
    d = SortedDict()
    d[3] = 'a'
    d[2] = 'b'
    d[1] = 'c'

    assert list(d.keys()) == [1, 2, 3]


def test_illegal_index():
    d = SortedDict()
    with pytest.raises(IndexError):
        d.index(0)

    with pytest.raises(TypeError):
        d.index('a')

    with pytest.raises(TypeError):
        d.index()


def test_empty_keys():
    d = SortedDict()
    assert d.keys() == ()


def test_keys_reference_counting():
    d = SortedDict()
    d[1] = 'a'
    assert d.keys() == (1,)

    d[2] = 'b'
    assert d.keys() == (1, 2)


def test_invalid_key():
    d = SortedDict()

    with pytest.raises(KeyError):
        d[1]

    with pytest.raises(KeyError):
        del d[1]


def test_del():
    d = SortedDict()
    d[3] = 'a'
    d[2] = 'b'
    d[1] = 'c'

    del d[2]

    assert d.keys() == (1, 3)


def test_iteration_noop():
    d = SortedDict()
    counter = 0

    for _ in d:
        counter += 1

    assert counter == 0


def test_invalid_depth():
    with pytest.raises(ValueError):
        SortedDict(max_depth=-1)

    with pytest.raises(ValueError):
        SortedDict(max_depth='A')


def test_invalid_truncate():
    with pytest.raises(ValueError):
        SortedDict(truncate=10)


def test_depth_members():
    d = SortedDict(max_depth=10, truncate=True)
    assert d.__max_depth == 10
    assert d.__truncate == 1

    e = SortedDict(max_depth=100, truncate=False)
    assert e.__max_depth == 100
    assert e.__truncate == 0

    f = SortedDict()
    assert f.__max_depth == 0
    assert f.__truncate == 0


def test_depth():
    d = SortedDict({i: i for i in range(100)}, max_depth=10)
    assert d.keys() == (0, 1, 2, 3, 4, 5, 6, 7, 8, 9)
    assert len(d) == 10

    assert len(d.to_dict()) == 10
    assert len(d.to_list()) == 10


def test_depth_nontruncated():
    d = SortedDict({i: i for i in range(100)}, max_depth=10)
    del d[5]
    assert d.keys() == (0, 1, 2, 3, 4, 6, 7, 8, 9, 10)


def test_depth_truncated():
    d = SortedDict({i: i for i in range(100)}, max_depth=10)
    d.truncate()

    del d[5]
    assert d.keys() == (0, 1, 2, 3, 4, 6, 7, 8, 9)


def test_depth_auto_truncate():
    d = SortedDict({i: i for i in range(100)}, truncate=True, max_depth=10)

    del d[5]
    assert d.keys() == (0, 1, 2, 3, 4, 6, 7, 8, 9)

    d[1.1] = 0
    d[1.2] = 0
    d[1.3] = 0
    assert d.keys() == (0, 1, 1.1, 1.2, 1.3, 2, 3, 4, 6, 7)


def test_to_dict_types():
    input = {
        1: 2,
        3.3: 4,
        Decimal('5.6'): 7.8,
        9: 11.11,
        Decimal('1.3'): Decimal('3.3'),
        77.8: Decimal('19.9')
    }
    d = SortedDict(input)

    assert d.to_dict() == input
    assert d.to_dict(to_type=str) == {'1': '2', '3.3': '4', '5.6': '7.8', '9': '11.11', '1.3': '3.3', '77.8': '19.9'}
    assert d.to_dict(from_type=str, to_type=float) == input
    assert d.to_dict(to_type=float, from_type=Decimal) == {1: 2, 3.3: 4, 5.6: 7.8, 9: 11.11, 1.3: 3.3, 77.8: 19.9}


def unorderable():
    '''
    A dict whose keys cannot be sorted against each other. Sorting is deferred
    until the keys are actually needed, so building this never raises.
    '''
    d = SortedDict()
    d[1] = 'a'
    d['b'] = 'c'
    return d


def test_depth_overflow():
    with pytest.raises(OverflowError):
        SortedDict(max_depth=2 ** 100)


def test_unencodable_ordering():
    # lone surrogates cannot be encoded to UTF-8
    with pytest.raises(UnicodeEncodeError):
        SortedDict(ordering='\ud800')


def test_unorderable_keys():
    with pytest.raises(TypeError):
        unorderable().keys()

    with pytest.raises(TypeError):
        unorderable().index(0)

    with pytest.raises(TypeError):
        unorderable().to_dict()

    with pytest.raises(TypeError):
        unorderable().to_list()

    with pytest.raises(TypeError):
        list(unorderable())


def test_unorderable_keys_truncate():
    d = SortedDict(max_depth=2)
    d[1] = 'a'
    d['b'] = 'c'

    with pytest.raises(TypeError):
        d.truncate()


def test_unorderable_keys_auto_truncate():
    # truncation happens on insert, so the failure surfaces from __setitem__
    d = SortedDict({1: 'a'}, max_depth=1, truncate=True)

    with pytest.raises(TypeError):
        d['b'] = 'c'


def test_unorderable_keys_init_truncate():
    with pytest.raises(TypeError):
        SortedDict({1: 'a', 'b': 'c'}, max_depth=1, truncate=True)


def test_to_dict_invalid_kwarg():
    with pytest.raises(TypeError):
        SortedDict().to_dict(bogus=1)


def test_to_dict_conversion_failure():
    # key cannot be converted
    with pytest.raises(ValueError):
        SortedDict({'x': 1}).to_dict(to_type=int)

    # value cannot be converted
    with pytest.raises(ValueError):
        SortedDict({1: 'x'}).to_dict(to_type=int)


def test_unhashable_key():
    d = SortedDict()

    with pytest.raises(TypeError):
        d[[1, 2]] = 3
