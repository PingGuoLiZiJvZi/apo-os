#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NYAN_MAGIC "NYANRLE1"
#define NYAN_FORMAT_RGB565_RLE 1u

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s <width> <height> <fps> <frames> <output.nyan>\n"
            "reads raw RGB24 frames from stdin and writes RGB565 RLE\n",
            argv0);
}

static int parse_u32(const char *s, uint32_t *out) {
    char *end = NULL;
    unsigned long v;

    errno = 0;
    v = strtoul(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v == 0 || v > 0xffffffffUL) {
        return 0;
    }
    *out = (uint32_t)v;
    return 1;
}

static int write_exact(FILE *fp, const void *buf, size_t len) {
    return fwrite(buf, 1, len, fp) == len;
}

static int write_le32(FILE *fp, uint32_t v) {
    uint8_t b[4];
    b[0] = (uint8_t)(v & 0xff);
    b[1] = (uint8_t)((v >> 8) & 0xff);
    b[2] = (uint8_t)((v >> 16) & 0xff);
    b[3] = (uint8_t)((v >> 24) & 0xff);
    return write_exact(fp, b, sizeof(b));
}

static void put_buf_le16(uint8_t *buf, size_t *pos, uint16_t v) {
    buf[(*pos)++] = (uint8_t)(v & 0xff);
    buf[(*pos)++] = (uint8_t)(v >> 8);
}

static uint16_t rgb565_from_rgb24(const uint8_t *p) {
    uint16_t r = (uint16_t)(p[0] >> 3);
    uint16_t g = (uint16_t)(p[1] >> 2);
    uint16_t b = (uint16_t)(p[2] >> 3);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static int read_frame(uint8_t *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        size_t n = fread(buf + off, 1, len - off, stdin);
        if (n == 0) {
            return 0;
        }
        off += n;
    }
    return 1;
}

static size_t encode_frame(const uint8_t *rgb, uint32_t pixels, uint8_t *out) {
    size_t pos = 0;
    uint32_t i = 0;

    while (i < pixels) {
        uint16_t color = rgb565_from_rgb24(rgb + (size_t)i * 3);
        uint32_t run = 1;

        while (i + run < pixels && run < 65535u) {
            uint16_t next = rgb565_from_rgb24(rgb + (size_t)(i + run) * 3);
            if (next != color) {
                break;
            }
            run++;
        }

        put_buf_le16(out, &pos, (uint16_t)run);
        put_buf_le16(out, &pos, color);
        i += run;
    }

    return pos;
}

int main(int argc, char **argv) {
    uint32_t width, height, fps, frames;
    uint64_t pixels64, frame_bytes64, rle_bytes64;
    size_t frame_bytes, rle_capacity;
    uint8_t *frame = NULL;
    uint8_t *rle = NULL;
    FILE *out = NULL;
    int ok = 0;

    if (argc != 6) {
        usage(argv[0]);
        return 2;
    }

    if (!parse_u32(argv[1], &width) ||
        !parse_u32(argv[2], &height) ||
        !parse_u32(argv[3], &fps) ||
        !parse_u32(argv[4], &frames)) {
        usage(argv[0]);
        return 2;
    }

    pixels64 = (uint64_t)width * (uint64_t)height;
    frame_bytes64 = pixels64 * 3ULL;
    rle_bytes64 = pixels64 * 4ULL;
    if (pixels64 == 0 || frame_bytes64 > (uint64_t)SIZE_MAX ||
        rle_bytes64 > (uint64_t)SIZE_MAX) {
        fprintf(stderr, "nyanpack: frame dimensions are too large\n");
        return 2;
    }

    frame_bytes = (size_t)frame_bytes64;
    rle_capacity = (size_t)rle_bytes64;
    frame = (uint8_t *)malloc(frame_bytes);
    rle = (uint8_t *)malloc(rle_capacity);
    if (!frame || !rle) {
        fprintf(stderr, "nyanpack: out of memory\n");
        goto done;
    }

    out = fopen(argv[5], "wb");
    if (!out) {
        fprintf(stderr, "nyanpack: cannot open %s: %s\n", argv[5], strerror(errno));
        goto done;
    }

    if (!write_exact(out, NYAN_MAGIC, 8) ||
        !write_le32(out, width) ||
        !write_le32(out, height) ||
        !write_le32(out, fps) ||
        !write_le32(out, frames) ||
        !write_le32(out, NYAN_FORMAT_RGB565_RLE) ||
        !write_le32(out, 0)) {
        fprintf(stderr, "nyanpack: failed to write header\n");
        goto done;
    }

    for (uint32_t i = 0; i < frames; i++) {
        size_t payload;

        if (!read_frame(frame, frame_bytes)) {
            fprintf(stderr, "nyanpack: input ended before frame %u\n", i);
            goto done;
        }

        payload = encode_frame(frame, (uint32_t)pixels64, rle);
        if (payload > 0xffffffffULL) {
            fprintf(stderr, "nyanpack: encoded frame is too large\n");
            goto done;
        }
        if (!write_le32(out, (uint32_t)payload) ||
            !write_exact(out, rle, payload)) {
            fprintf(stderr, "nyanpack: failed to write frame %u\n", i);
            goto done;
        }
    }

    if (fclose(out) != 0) {
        out = NULL;
        fprintf(stderr, "nyanpack: failed to close output\n");
        goto done;
    }
    out = NULL;
    ok = 1;

done:
    if (out) {
        fclose(out);
    }
    free(frame);
    free(rle);
    return ok ? 0 : 1;
}
