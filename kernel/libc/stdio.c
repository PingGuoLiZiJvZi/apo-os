#include "stdio.h"
#include "../device/device.h"

void putchar(char c)
{
    uart_putchar(c);
}

void puts(const char *s)
{
    while (*s) {
        putchar(*s);
        s++;
    }
}


static void print_dec(long val, int is_signed)
{
    char buf[21]; 
    int i = 0;
    int neg = 0;
    unsigned long uval;

    if (is_signed && val < 0) {
        neg = 1;
        uval = (unsigned long)(-val);
    } else {
        uval = (unsigned long)val;
    }

    if (uval == 0) {
        putchar('0');
        return;
    }

    while (uval > 0) {
        buf[i++] = '0' + (uval % 10);
        uval /= 10;
    }

    if (neg)
        putchar('-');

    while (i > 0)
        putchar(buf[--i]);
}

static void print_hex(unsigned long val)
{
    static const char hex[] = "0123456789abcdef";
    char buf[17];
    int i = 0;

    if (val == 0) {
        putchar('0');
        return;
    }

    while (val > 0) {
        buf[i++] = hex[val & 0xf];
        val >>= 4;
    }

    while (i > 0)
        putchar(buf[--i]);
}


int printf(const char *fmt, ...)
{
    va_list ap;
    int count = 0;

    va_start(ap, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            putchar(*fmt);
            count++;
            fmt++;
            continue;
        }

        fmt++; /* skip '%' */

        switch (*fmt) {
        case 'l':
            fmt++; // skip 'l'
            if (*fmt == 'x') {
                unsigned long val = va_arg(ap, unsigned long);
                print_hex(val);
            } else if (*fmt == 'd') {
                long val = va_arg(ap, long);
                print_dec(val, 1);
            } else if (*fmt == 'u') {
                unsigned long val = va_arg(ap, unsigned long);
                print_dec((long)val, 0);
            } else {
                putchar('%');
                putchar('l');
                putchar(*fmt);
                count += 3;
            }
            break;
        case 'd': {
            long val = va_arg(ap, int);
            print_dec(val, 1);
            break;
        }
        case 'u': {
            unsigned long val = va_arg(ap, unsigned int);
            print_dec((long)val, 0);
            break;
        }
        case 'x': {
            unsigned long val = va_arg(ap, unsigned int);
            print_hex(val);
            break;
        }
        case 'p': {
            unsigned long val = (unsigned long)va_arg(ap, void *);
            puts("0x");
            print_hex(val);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            puts(s);
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            putchar(c);
            count++;
            break;
        }
        case '%':
            putchar('%');
            count++;
            break;
        default:
            putchar('%');
            putchar(*fmt);
            count += 2;
            break;
        }

        fmt++;
    }

    va_end(ap);
    return count;
}
