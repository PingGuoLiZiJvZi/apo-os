#include "fdt.h"
#include "../libc/stdio.h"

/* ---- 大小端转换 (FDT 为大端) ---- */

static inline uint32_t be32(uint32_t v)
{
    return ((v & 0xff) << 24) |
           ((v & 0xff00) << 8) |
           ((v & 0xff0000) >> 8) |
           ((v >> 24) & 0xff);
}

static inline uint64_t be64_from(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    return ((uint64_t)b[0] << 56) | ((uint64_t)b[1] << 48) |
           ((uint64_t)b[2] << 40) | ((uint64_t)b[3] << 32) |
           ((uint64_t)b[4] << 24) | ((uint64_t)b[5] << 16) |
           ((uint64_t)b[6] << 8)  |  (uint64_t)b[7];
}

/* ---- 字符串工具 ---- */

static int str_len(const char *s)
{
    int n = 0;
    while (*s++) n++;
    return n;
}

/* 4 字节对齐 */
static inline uint32_t align4(uint32_t v)
{
    return (v + 3) & ~3u;
}

/* ---- 打印缩进 ---- */

static void print_indent(int depth)
{
    for (int i = 0; i < depth; i++)
        puts("  ");
}

/* ---- 打印属性值 ---- */

/* 判断是否看起来像可打印字符串 */
static int is_printable_string(const uint8_t *data, int len)
{
    if (len == 0)
        return 0;
    /* 最后一个字节必须是 '\0' */
    if (data[len - 1] != '\0')
        return 0;
    for (int i = 0; i < len - 1; i++) {
        char c = (char)data[i];
        /* 允许可打印字符和换行 */
        if (c == '\0') continue; /* 字符串列表中的分隔符 */
        if (c < 0x20 || c > 0x7e)
            return 0;
    }
    return 1;
}

static void print_prop_value(const uint8_t *data, int len)
{
    if (len == 0) {
        /* boolean 属性，无值 */
        return;
    }

    if (is_printable_string(data, len)) {
        /* 字符串或字符串列表 */
        puts(" = \"");
        const uint8_t *p = data;
        const uint8_t *end = data + len;
        while (p < end) {
            if (*p == '\0') {
                if (p + 1 < end) {
                    puts("\", \"");
                }
            } else {
                putchar((char)*p);
            }
            p++;
        }
        putchar('"');
        return;
    }

    if (len % 4 == 0) {
        /* u32 数组 */
        puts(" = <");
        for (int i = 0; i < len; i += 4) {
            if (i > 0) putchar(' ');
            uint32_t v = be32(*(const uint32_t *)(data + i));
            puts("0x");
            /* 打印完整 8 位十六进制 */
            static const char hex[] = "0123456789abcdef";
            for (int j = 28; j >= 0; j -= 4)
                putchar(hex[(v >> j) & 0xf]);
        }
        putchar('>');
        return;
    }

    /* 原始字节 */
    puts(" = [");
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < len; i++) {
        if (i > 0) putchar(' ');
        putchar(hex[data[i] >> 4]);
        putchar(hex[data[i] & 0xf]);
    }
    putchar(']');
}

/* ---- 公开 API ---- */

int fdt_check(const void *fdt)
{
    const struct fdt_header *hdr = (const struct fdt_header *)fdt;
    if (be32(hdr->magic) != FDT_MAGIC)
        return -1;
    return 0;
}

void fdt_print(const void *fdt)
{
    const struct fdt_header *hdr = (const struct fdt_header *)fdt;
    uint32_t struct_off  = be32(hdr->off_dt_struct);
    uint32_t strings_off = be32(hdr->off_dt_strings);

    const char *strings = (const char *)fdt + strings_off;
    const uint32_t *p = (const uint32_t *)((const char *)fdt + struct_off);

    printf("Device Tree: size = %d bytes, version = %d\n",
           be32(hdr->totalsize), be32(hdr->version));
    printf("----------------------------------------\n");

    int depth = 0;

    while (1) {
        uint32_t token = be32(*p);
        p++;

        switch (token) {
        case FDT_BEGIN_NODE: {
            const char *name = (const char *)p;
            int name_len = str_len(name);
            print_indent(depth);
            if (name_len == 0)
                printf("/");
            else
                printf("%s", name);
            printf(" {\n");
            /* 跳过名称字符串（含 '\0'，4 字节对齐） */
            p = (const uint32_t *)((const char *)p + align4(name_len + 1));
            depth++;
            break;
        }
        case FDT_END_NODE:
            depth--;
            print_indent(depth);
            printf("}\n");
            break;
        case FDT_PROP: {
            uint32_t len     = be32(p[0]);
            uint32_t nameoff = be32(p[1]);
            p += 2;
            const uint8_t *data = (const uint8_t *)p;
            const char *prop_name = strings + nameoff;
            print_indent(depth);
            printf("%s", prop_name);
            print_prop_value(data, len);
            printf(";\n");
            /* 跳过数据（4 字节对齐） */
            p = (const uint32_t *)((const uint8_t *)p + align4(len));
            break;
        }
        case FDT_NOP:
            break;
        case FDT_END:
            printf("----------------------------------------\n");
            return;
        default:
            printf("[unknown token 0x%x]\n", token);
            return;
        }
    }
}
