from decimal import Decimal
import random

import pytest

from orderbook import SortedDict


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
