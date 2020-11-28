from distutils.core import setup, Extension

orderbook = Extension('orderbook', sources = ['orderbook/orderbook.c'])

setup (name = 'orderbook',
       version = '0.0.1',
       description = 'An orderbook data structure',
       ext_modules = [orderbook])
