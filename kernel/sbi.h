#ifndef __SBI_H__
#define __SBI_H__

#include <stdint.h>

struct sbi_ret {
    long error;
    long value;
};

static inline struct sbi_ret sbi_call(long ext, long fid,
                                      long arg0, long arg1, long arg2)
{
    struct sbi_ret ret;
    register long a0 asm("a0") = arg0;
    register long a1 asm("a1") = arg1;
    register long a2 asm("a2") = arg2;
    register long a6 asm("a6") = fid;
    register long a7 asm("a7") = ext;

    asm volatile(
        "ecall"
        : "+r"(a0), "+r"(a1)
        : "r"(a2), "r"(a6), "r"(a7)
        : "memory"
    );

    ret.error = a0;
    ret.value = a1;
    return ret;
}

/* Legacy SBI Console Putchar (Extension 0x01) */
static inline void sbi_console_putchar(int ch)
{
    sbi_call(0x01, 0, ch, 0, 0);
}

/* SBI System Reset Extension (SRST, 0x53525354) */
#define SBI_EXT_SRST            0x53525354
#define SBI_SRST_RESET_TYPE_SHUTDOWN    0x00000000
#define SBI_SRST_RESET_TYPE_COLD_REBOOT 0x00000001
#define SBI_SRST_RESET_TYPE_WARM_REBOOT 0x00000002
#define SBI_SRST_RESET_REASON_NONE      0x00000000
#define SBI_SRST_RESET_REASON_SYSFAIL   0x00000001

static inline void sbi_system_reset(unsigned int reset_type,
                                     unsigned int reset_reason)
{
    sbi_call(SBI_EXT_SRST, 0,
             (long)reset_type, (long)reset_reason, 0);
    /* 如果 SBI 调用成功，这里不可达 */
}

#endif /* __SBI_H__ */
