/*
Copyright (C) 2020-2021  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.
*/

enum side_e {
    BID,
    ASK,
    INVALID_SIDE
};

enum side_e check_key(const char *key);
