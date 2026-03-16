#ifndef __NDL_IPC_H__
#define __NDL_IPC_H__

#include <stdint.h>

#define NDL_IPC_MAGIC 0x4E444C31u
#define NDL_IPC_TAG   0x4C4Du

#define NDL_IPC_C2W_HELLO 1u
#define NDL_IPC_C2W_DRAW  2u

#define NDL_IPC_W2C_EVENT 101u
#define NDL_IPC_W2C_FOCUS 102u

typedef struct {
  uint32_t magic;
  uint16_t type;
  uint16_t reserved;
  int32_t x;
  int32_t y;
  int32_t w;
  int32_t h;
} NDLIPCMsg;

#endif
