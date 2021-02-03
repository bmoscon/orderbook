/*
Copyright (C) 2020-2021  Bryant Moscon - bmoscon@gmail.com

Please see the LICENSE file for the terms and conditions
associated with this software.
*/
#include <string.h>
#include "utils.h"


enum side_e check_key(const char *key)
{
    if (!strcmp(key, "bid") || !strcmp(key, "BID") || !strcmp(key, "bids") || !strcmp(key, "BIDS")) {
        return BID;
    }

    if (!strcmp(key, "ask") || !strcmp(key, "ASK") || !strcmp(key, "asks") || !strcmp(key, "ASKS")) {
        return ASK;
    }

    return INVALID_SIDE;
}
