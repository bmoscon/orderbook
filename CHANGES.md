## Changelog

### 0.2.1 (2021-03-29)
  * Bugfix: Invalid deallocation of python object

### 0.2.0 (2021-03-12)
  * Feature: Add branch prediction hints around error handling code
  * Bugfix: Fix regression from adding branch predictors
  * Bugfix: Fix error corner case when iterating twice on an empty dataset
  * Feature: Add contains function for membership test
  * Bugfix: Fix issues around storing L3 data
  * Feature: Enhance testing, add in L3 book test cases

### 0.1.1 (2021-02-12)
  * Feature: Checksum support for orderbooks
  * Feature: FTX checksum support
  * Feature: Kraken checksum support
  * Feature: OkEX/OKCoin checksum support
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
