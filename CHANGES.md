## Changelog

### 0.4.3 (2022-05-29)
 * Bugfix: handle scientific notation of small values in Kraken checksum
 * Update: calculate Kraken checksum on order books less than 10 levels deep
 * Bugfix: fix occasional incorrect checksums for OKX, FTX and Bitget

### 0.4.2 (2022-04-17)
 * Update: OKEx renamed OKX (for checksum validation)
 * Feature: Add support for orderbook checksums with Bitget

### 0.4.1 (2021-10-12)
 * Bugfix: unnecessary reference counting prevented sorted dictionaries from being deallocated
 * Bugfix: setting ordering on a sorted dict before checking that it was created successfully

### 0.4.0 (2021-09-16)
 * Feature: changes to code and setup.py to enable compiling on windows
 * Feature: add from_type/to_type kwargs to the to_dict methods, allowing for type conversion when creating the dictionary

### 0.3.2 (2021-09-04)
 * Bugfix: depth was incorrectly ignored when converting sorteddict to python dict

### 0.3.1 (2021-09-01)
  * Bugfix: truncate and max_depth not being passed from orderbook to sorteddict object correctly
  * Feature: let checksum_format kwarg be set to None

### 0.3.0 (2021-07-16)
  * Update classifiers to indicate this projects only supports MacOS/Linux
  * Bugfix: Using less than the minimum number of levels for a checksum with Kraken not raising error correctly
  * Update: add del examples to test code

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
