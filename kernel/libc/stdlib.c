#include "stdlib.h"

unsigned long strtoul(const char *nptr, char **endptr, int base)
{
    const char *s = nptr;
    unsigned long acc;
    int c;
    unsigned long cutoff;
    int neg = 0, any, cutlim;

    do {
        c = *s++;
    } while (c == ' ' || c == '\t' || c == '\n' || c == '\r');

    if (c == '-') {
        neg = 1;
        c = *s++;
    } else if (c == '+') {
        c = *s++;
    }

    if ((base == 0 || base == 16) &&
        c == '0' && (*s == 'x' || *s == 'X')) {
        c = s[1];
        s += 2;
        base = 16;
    }

    if (base == 0) {
        base = c == '0' ? 8 : 10;
    }

    /* We don't have ULONG_MAX macro, so we approximate it (0xFFFFFFFF or 0xFFFFFFFFFFFFFFFF) */
    cutoff = ~0UL;
    cutlim = cutoff % (unsigned long)base;
    cutoff /= (unsigned long)base;

    for (acc = 0, any = 0;; c = *s++) {
        if (c >= '0' && c <= '9') {
            c -= '0';
        } else if (c >= 'a' && c <= 'z') {
            c -= 'a' - 10;
        } else if (c >= 'A' && c <= 'Z') {
            c -= 'A' - 10;
        } else {
            break;
        }

        if (c >= base) {
            break;
        }

        if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim)) {
            any = -1;
        } else {
            any = 1;
            acc *= base;
            acc += c;
        }
    }

    if (any < 0) {
        acc = ~0UL;
    } else if (neg) {
        acc = -acc;
    }

    if (endptr != 0) {
        *endptr = (char *)(any ? s - 1 : nptr);
    }
    
    return acc;
}
