#ifndef __FDT_H__
#define __FDT_H__

#include <stdint.h>

/* FDT Header */
struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

#define FDT_MAGIC       0xd00dfeed
#define FDT_BEGIN_NODE  0x00000001
#define FDT_END_NODE    0x00000002
#define FDT_PROP        0x00000003
#define FDT_NOP         0x00000004
#define FDT_END         0x00000009

/* 验证 FDT 头，成功返回 0 */
int fdt_check(const void *fdt);

/* 打印整棵设备树（带缩进的树形结构） */
void fdt_print(const void *fdt);

#endif /* __FDT_H__ */
