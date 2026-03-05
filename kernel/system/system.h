#ifndef __SYSTEM_H__
#define __SYSTEM_H__

/* 打印 panic 信息并死循环 */
void panic(const char *msg);

/* 通过 SBI SRST 关机 */
void shutdown(void);

/* 通过 SBI SRST 冷重启 */
void reboot(void);

#endif /* __SYSTEM_H__ */
