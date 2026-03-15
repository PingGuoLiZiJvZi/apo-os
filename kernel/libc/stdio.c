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


typedef struct {
    int to_buf;
    char *buf;
    int pos;
} OutCtx;

static void outc(OutCtx *ctx, char c) {
    if (ctx->to_buf) {
        ctx->buf[ctx->pos] = c;
    } else {
        putchar(c);
    }
    ctx->pos++;
}

static void outs(OutCtx *ctx, const char *s) {
    if (!s) s = "(null)";
    while (*s) {
        outc(ctx, *s++);
    }
}

static void out_dec(OutCtx *ctx, long val, int is_signed) {
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
        outc(ctx, '0');
        return;
    }

    while (uval > 0) {
        buf[i++] = '0' + (uval % 10);
        uval /= 10;
    }

    if (neg) outc(ctx, '-');
    while (i > 0) outc(ctx, buf[--i]);
}

static void out_hex(OutCtx *ctx, unsigned long val) {
    static const char hex[] = "0123456789abcdef";
    char buf[17];
    int i = 0;

    if (val == 0) {
        outc(ctx, '0');
        return;
    }

    while (val > 0) {
        buf[i++] = hex[val & 0xf];
        val >>= 4;
    }

    while (i > 0) outc(ctx, buf[--i]);
}

static int vformat(OutCtx *ctx, const char *fmt, va_list ap) {
    while (*fmt) {
        if (*fmt != '%') {
            outc(ctx, *fmt++);
            continue;
        }

        fmt++;
        switch (*fmt) {
        case 'l':
            fmt++;
            if (*fmt == 'x') {
                unsigned long val = va_arg(ap, unsigned long);
                out_hex(ctx, val);
            } else if (*fmt == 'd') {
                long val = va_arg(ap, long);
                out_dec(ctx, val, 1);
            } else if (*fmt == 'u') {
                unsigned long val = va_arg(ap, unsigned long);
                out_dec(ctx, (long)val, 0);
            } else {
                outc(ctx, '%');
                outc(ctx, 'l');
                outc(ctx, *fmt);
            }
            break;
        case 'd': {
            long val = va_arg(ap, int);
            out_dec(ctx, val, 1);
            break;
        }
        case 'u': {
            unsigned long val = va_arg(ap, unsigned int);
            out_dec(ctx, (long)val, 0);
            break;
        }
        case 'x': {
            unsigned long val = va_arg(ap, unsigned int);
            out_hex(ctx, val);
            break;
        }
        case 'p': {
            unsigned long val = (unsigned long)va_arg(ap, void *);
            outc(ctx, '0');
            outc(ctx, 'x');
            out_hex(ctx, val);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            outs(ctx, s);
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            outc(ctx, c);
            break;
        }
        case '%':
            outc(ctx, '%');
            break;
        default:
            outc(ctx, '%');
            outc(ctx, *fmt);
            break;
        }
        fmt++;
    }
    return ctx->pos;
}


int printf(const char *fmt, ...)
{
    va_list ap;
    OutCtx ctx = {.to_buf = 0, .buf = 0, .pos = 0};

    va_start(ap, fmt);
    int count = vformat(&ctx, fmt, ap);
    va_end(ap);
    return count;
}

int sprintf(char *out, const char *fmt, ...)
{
    if (!out || !fmt) return -1;

    va_list ap;
    OutCtx ctx = {.to_buf = 1, .buf = out, .pos = 0};

    va_start(ap, fmt);
    int count = vformat(&ctx, fmt, ap);
    va_end(ap);

    out[count] = '\0';
    return count;
}
