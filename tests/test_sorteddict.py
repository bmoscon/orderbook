'''
Copyright (C) 2020-2024  Bryant Moscon - bmoscon@gmail.com

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
