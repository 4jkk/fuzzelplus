#pragma once

#include <stdint.h>

enum {
    HEX_DIGIT_INVALID = 16
};

static inline uint8_t
hex2nibble(char c)
{
    switch (c) {
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
        return c - '0';

    case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
        return c - 'a' + 10;

    case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
        return c - 'A' + 10;
    }

    return HEX_DIGIT_INVALID;
}

