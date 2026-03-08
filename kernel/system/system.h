#ifndef __SYSTEM_H__
#define __SYSTEM_H__

void panic(const char *msg);
void shutdown();
void reboot();
void yield();

#endif /* __SYSTEM_H__ */
