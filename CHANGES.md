## Changelog

### 0.1.1
  * Feature: Checksum support for orderbooks
  * Feature: FTX checksum support
  * Feature: Kraken checksum support
  * Perf: Use CRC32 table to improve performance of checksum code

### 0.1.0 (2021-01-18)
  * Minor: Use enums to make code more readable
  * Bugfix: Add manifest file to ensure headers and changes file are included in sdist builds
  * Feature: Add support for max depth and depth truncation

### 0.0.2 (2020-12-27)
  * Bugfix: Fix sorted dictionary arg parsing
  * Feature: Coverage report generation for C library
  * Bugfix: Fix reference counting in index method in SortedDict
  * Feature: New unit tests to improve SortedDict coverage
  * Feature: Modularize files
  * Feature: Add ability to set bids/asks to dictionaries via attributes or \[ \]
  * Docs: Update README with simple usage example

### 0.0.1 (2020-12-26)
  * Initial Release
