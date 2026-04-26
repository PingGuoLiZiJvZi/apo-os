#include <am.h>

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

Area heap = {0};

#define KEYDOWN_MASK 0x8000
#define KEY_QUEUE_LEN 128

static int g_inited = 0;
static int g_evfd = -1;
static int g_fbfd = -1;
static int g_fbsyncfd = -1;
static int g_audiofd = -1;
static int g_serialfd = -1;
static int g_screen_w = 640;
static int g_screen_h = 480;
static uint64_t g_boot_us = 0;
static int g_key_queue[KEY_QUEUE_LEN];
static int g_key_head = 0;
static int g_key_tail = 0;

static int g_audio_freq = 44100;
static int g_audio_channels = 2;
static int g_audio_samples = 1024;
static int g_audio_pending_bytes = 0;
static uint64_t g_audio_last_us = 0;

static uint64_t now_us(void);
static int linux_keycode_to_am(int code);

static void audio_update_pending(void) {
  uint64_t now = now_us();
  if (g_audio_last_us == 0) {
    g_audio_last_us = now;
    return;
  }

  if (now <= g_audio_last_us) return;

  uint64_t delta = now - g_audio_last_us;
  uint64_t bytes_per_sec = (uint64_t)g_audio_freq * (uint64_t)g_audio_channels * 2ULL;
  uint64_t consumed = (delta * bytes_per_sec) / 1000000ULL;
  if ((int)consumed > g_audio_pending_bytes) {
    g_audio_pending_bytes = 0;
  } else {
    g_audio_pending_bytes -= (int)consumed;
  }
  g_audio_last_us = now;
}

static int keyq_empty(void) {
  return g_key_head == g_key_tail;
}

static void keyq_push(int value) {
  int next_tail = (g_key_tail + 1) % KEY_QUEUE_LEN;
  if (next_tail == g_key_head) return;
  g_key_queue[g_key_tail] = value;
  g_key_tail = next_tail;
}

static int keyq_pop(void) {
  if (keyq_empty()) return AM_KEY_NONE;
  int value = g_key_queue[g_key_head];
  g_key_head = (g_key_head + 1) % KEY_QUEUE_LEN;
  return value;
}

static void pump_input_events(void) {
  if (g_evfd < 0) return;

  for (int i = 0; i < 32; i++) {
    char ev[64] = {0};
    int n = read(g_evfd, ev, sizeof(ev) - 1);
    if (n <= 0) break;
    ev[n] = '\0';

    int code = 0;
    if (sscanf(ev, "kd %d", &code) == 1) {
      int am_code = linux_keycode_to_am(code);
      if (am_code != AM_KEY_NONE) keyq_push(am_code | KEYDOWN_MASK);
      continue;
    }
    if (sscanf(ev, "ku %d", &code) == 1) {
      int am_code = linux_keycode_to_am(code);
      if (am_code != AM_KEY_NONE) keyq_push(am_code);
      continue;
    }
  }
}

static uint64_t now_us(void) {
  struct timeval tv;
  if (gettimeofday(&tv, 0) != 0) return 0;
  return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

static int linux_keycode_to_am(int code) {
  switch (code) {
    case 1: return AM_KEY_ESCAPE;
    case 2: return AM_KEY_1;
    case 3: return AM_KEY_2;
    case 4: return AM_KEY_3;
    case 5: return AM_KEY_4;
    case 6: return AM_KEY_5;
    case 7: return AM_KEY_6;
    case 8: return AM_KEY_7;
    case 9: return AM_KEY_8;
    case 10: return AM_KEY_9;
    case 11: return AM_KEY_0;
    case 12: return AM_KEY_MINUS;
    case 13: return AM_KEY_EQUALS;
    case 14: return AM_KEY_BACKSPACE;
    case 15: return AM_KEY_TAB;
    case 16: return AM_KEY_Q;
    case 17: return AM_KEY_W;
    case 18: return AM_KEY_E;
    case 19: return AM_KEY_R;
    case 20: return AM_KEY_T;
    case 21: return AM_KEY_Y;
    case 22: return AM_KEY_U;
    case 23: return AM_KEY_I;
    case 24: return AM_KEY_O;
    case 25: return AM_KEY_P;
    case 26: return AM_KEY_LEFTBRACKET;
    case 27: return AM_KEY_RIGHTBRACKET;
    case 28: return AM_KEY_RETURN;
    case 29: return AM_KEY_LCTRL;
    case 30: return AM_KEY_A;
    case 31: return AM_KEY_S;
    case 32: return AM_KEY_D;
    case 33: return AM_KEY_F;
    case 34: return AM_KEY_G;
    case 35: return AM_KEY_H;
    case 36: return AM_KEY_J;
    case 37: return AM_KEY_K;
    case 38: return AM_KEY_L;
    case 39: return AM_KEY_SEMICOLON;
    case 40: return AM_KEY_APOSTROPHE;
    case 41: return AM_KEY_GRAVE;
    case 42: return AM_KEY_LSHIFT;
    case 43: return AM_KEY_BACKSLASH;
    case 44: return AM_KEY_Z;
    case 45: return AM_KEY_X;
    case 46: return AM_KEY_C;
    case 47: return AM_KEY_V;
    case 48: return AM_KEY_B;
    case 49: return AM_KEY_N;
    case 50: return AM_KEY_M;
    case 51: return AM_KEY_COMMA;
    case 52: return AM_KEY_PERIOD;
    case 53: return AM_KEY_SLASH;
    case 54: return AM_KEY_RSHIFT;
    case 56: return AM_KEY_LALT;
    case 57: return AM_KEY_SPACE;
    case 58: return AM_KEY_CAPSLOCK;
    case 59: return AM_KEY_F1;
    case 60: return AM_KEY_F2;
    case 61: return AM_KEY_F3;
    case 62: return AM_KEY_F4;
    case 63: return AM_KEY_F5;
    case 64: return AM_KEY_F6;
    case 65: return AM_KEY_F7;
    case 66: return AM_KEY_F8;
    case 67: return AM_KEY_F9;
    case 68: return AM_KEY_F10;
    case 87: return AM_KEY_F11;
    case 88: return AM_KEY_F12;
    case 97: return AM_KEY_RCTRL;
    case 100: return AM_KEY_RALT;
    case 102: return AM_KEY_HOME;
    case 103: return AM_KEY_UP;
    case 104: return AM_KEY_PAGEUP;
    case 105: return AM_KEY_LEFT;
    case 106: return AM_KEY_RIGHT;
    case 107: return AM_KEY_END;
    case 108: return AM_KEY_DOWN;
    case 109: return AM_KEY_PAGEDOWN;
    case 110: return AM_KEY_INSERT;
    case 111: return AM_KEY_DELETE;
    case 117: return AM_KEY_APPLICATION;
    default: return AM_KEY_NONE;
  }
}

static void parse_dispinfo(void) {
  int infofd = open("/device/dispinfo", O_RDONLY, 0);
  if (infofd < 0) return;

  char buf[64] = {0};
  int n = read(infofd, buf, sizeof(buf) - 1);
  if (n > 0) {
    buf[n] = '\0';
    int w = 0, h = 0;
    if (sscanf(buf, "WIDTH:%d\nHEIGHT:%d\n", &w, &h) == 2 && w > 0 && h > 0) {
      g_screen_w = w;
      g_screen_h = h;
    }
  }
  close(infofd);
}

void putch(char ch) {
  write(1, &ch, 1);
}

void halt(int code) {
  _exit(code);
}

bool ioe_init(void) {
  if (g_inited) return true;

  g_evfd = open("/device/events", O_RDONLY, 0);
  g_fbfd = open("/device/fb", O_WRONLY, 0);
  g_fbsyncfd = open("/device/fbsync", O_WRONLY, 0);
  g_audiofd = open("/device/audio", O_WRONLY, 0);
  g_serialfd = open("/device/serial", O_RDONLY, 0);

  parse_dispinfo();
  g_boot_us = now_us();
  g_audio_last_us = g_boot_us;
  g_key_head = g_key_tail = 0;
  g_inited = 1;
  return true;
}

static void am_uart_config(void *buf) {
  AM_UART_CONFIG_T *cfg = (AM_UART_CONFIG_T *)buf;
  cfg->present = true;
}

static void am_uart_tx(void *buf) {
  AM_UART_TX_T *tx = (AM_UART_TX_T *)buf;
  write(1, &tx->data, 1);
}

static void am_uart_rx(void *buf) {
  AM_UART_RX_T *rx = (AM_UART_RX_T *)buf;
  rx->data = 0;
  if (g_serialfd < 0) return;
  char c = 0;
  if (read(g_serialfd, &c, 1) == 1) rx->data = c;
}

static void am_timer_config(void *buf) {
  AM_TIMER_CONFIG_T *cfg = (AM_TIMER_CONFIG_T *)buf;
  cfg->present = true;
  cfg->has_rtc = true;
}

static void am_timer_rtc(void *buf) {
  AM_TIMER_RTC_T *rtc = (AM_TIMER_RTC_T *)buf;
  time_t t = time(0);
  struct tm *tmv = localtime(&t);
  if (!tmv) {
    rtc->year = rtc->month = rtc->day = 0;
    rtc->hour = rtc->minute = rtc->second = 0;
    return;
  }

  rtc->year = tmv->tm_year + 1900;
  rtc->month = tmv->tm_mon + 1;
  rtc->day = tmv->tm_mday;
  rtc->hour = tmv->tm_hour;
  rtc->minute = tmv->tm_min;
  rtc->second = tmv->tm_sec;
}

static void am_timer_uptime(void *buf) {
  AM_TIMER_UPTIME_T *uptime = (AM_TIMER_UPTIME_T *)buf;
  uint64_t now = now_us();
  uptime->us = (now >= g_boot_us) ? (now - g_boot_us) : 0;
}

static void am_input_config(void *buf) {
  AM_INPUT_CONFIG_T *cfg = (AM_INPUT_CONFIG_T *)buf;
  cfg->present = (g_evfd >= 0);
}

static void am_input_keybrd(void *buf) {
  AM_INPUT_KEYBRD_T *kbd = (AM_INPUT_KEYBRD_T *)buf;
  kbd->keydown = false;
  kbd->keycode = AM_KEY_NONE;

  pump_input_events();

  int value = keyq_pop();
  if (value == AM_KEY_NONE) return;

  kbd->keydown = ((value & KEYDOWN_MASK) != 0);
  kbd->keycode = (value & ~KEYDOWN_MASK);
}

static void am_gpu_config(void *buf) {
  AM_GPU_CONFIG_T *cfg = (AM_GPU_CONFIG_T *)buf;
  cfg->present = (g_fbfd >= 0);
  cfg->has_accel = false;
  cfg->width = g_screen_w;
  cfg->height = g_screen_h;
  cfg->vmemsz = g_screen_w * g_screen_h * 4;
}

static void am_gpu_status(void *buf) {
  AM_GPU_STATUS_T *st = (AM_GPU_STATUS_T *)buf;
  st->ready = true;
}

static void am_gpu_fbdraw(void *buf) {
  AM_GPU_FBDRAW_T *ctl = (AM_GPU_FBDRAW_T *)buf;
  if (g_fbfd >= 0 && ctl->pixels && ctl->w > 0 && ctl->h > 0) {
    uint8_t *pixels = (uint8_t *)ctl->pixels;
    for (int row = 0; row < ctl->h; row++) {
      off_t off = (off_t)(((ctl->y + row) * g_screen_w + ctl->x) * 4);
      if (lseek(g_fbfd, off, SEEK_SET) < 0) return;
      int len = ctl->w * 4;
      write(g_fbfd, pixels + row * len, (size_t)len);
    }
  }

  if (ctl->sync && g_fbsyncfd >= 0) {
    char ch = 0;
    write(g_fbsyncfd, &ch, 1);
  }
}

static void am_audio_config(void *buf) {
  AM_AUDIO_CONFIG_T *cfg = (AM_AUDIO_CONFIG_T *)buf;
  cfg->present = (g_audiofd >= 0);
  cfg->bufsize = 65536;
}

static void am_audio_ctrl(void *buf) {
  AM_AUDIO_CTRL_T *ctrl = (AM_AUDIO_CTRL_T *)buf;
  if (ctrl->freq > 0) g_audio_freq = ctrl->freq;
  if (ctrl->channels > 0) g_audio_channels = ctrl->channels;
  if (ctrl->samples > 0) g_audio_samples = ctrl->samples;
  g_audio_pending_bytes = 0;
  g_audio_last_us = now_us();
}

static void am_audio_status(void *buf) {
  AM_AUDIO_STATUS_T *st = (AM_AUDIO_STATUS_T *)buf;
  audio_update_pending();
  st->count = g_audio_pending_bytes;
}

static void am_audio_play(void *buf) {
  AM_AUDIO_PLAY_T *play = (AM_AUDIO_PLAY_T *)buf;
  if (g_audiofd < 0 || !play->buf.start || !play->buf.end) return;
  audio_update_pending();
  size_t len = (size_t)((uintptr_t)play->buf.end - (uintptr_t)play->buf.start);
  if ((intptr_t)len <= 0) return;
  int written = write(g_audiofd, play->buf.start, len);
  if (written > 0) g_audio_pending_bytes += written;
}

void ioe_read(int reg, void *buf) {
  if (!g_inited) ioe_init();

  switch (reg) {
    case AM_UART_CONFIG: am_uart_config(buf); break;
    case AM_UART_RX: am_uart_rx(buf); break;
    case AM_TIMER_CONFIG: am_timer_config(buf); break;
    case AM_TIMER_RTC: am_timer_rtc(buf); break;
    case AM_TIMER_UPTIME: am_timer_uptime(buf); break;
    case AM_INPUT_CONFIG: am_input_config(buf); break;
    case AM_INPUT_KEYBRD: am_input_keybrd(buf); break;
    case AM_GPU_CONFIG: am_gpu_config(buf); break;
    case AM_GPU_STATUS: am_gpu_status(buf); break;
    case AM_AUDIO_CONFIG: am_audio_config(buf); break;
    case AM_AUDIO_STATUS: am_audio_status(buf); break;
    case AM_DISK_CONFIG: {
      AM_DISK_CONFIG_T *cfg = (AM_DISK_CONFIG_T *)buf;
      cfg->present = false;
      cfg->blksz = 0;
      cfg->blkcnt = 0;
      break;
    }
    case AM_DISK_STATUS: {
      AM_DISK_STATUS_T *st = (AM_DISK_STATUS_T *)buf;
      st->ready = false;
      break;
    }
    case AM_NET_CONFIG: {
      AM_NET_CONFIG_T *cfg = (AM_NET_CONFIG_T *)buf;
      cfg->present = false;
      break;
    }
    case AM_NET_STATUS: {
      AM_NET_STATUS_T *st = (AM_NET_STATUS_T *)buf;
      st->rx_len = 0;
      st->tx_len = 0;
      break;
    }
    default: break;
  }
}

void ioe_write(int reg, void *buf) {
  if (!g_inited) ioe_init();

  switch (reg) {
    case AM_UART_TX: am_uart_tx(buf); break;
    case AM_GPU_FBDRAW: am_gpu_fbdraw(buf); break;
    case AM_AUDIO_CTRL: am_audio_ctrl(buf); break;
    case AM_AUDIO_PLAY: am_audio_play(buf); break;
    case AM_DISK_BLKIO:
    case AM_NET_TX:
    case AM_NET_RX:
      break;
    default: break;
  }
}

bool cte_init(Context *(*handler)(Event ev, Context *ctx)) {
  (void)handler;
  return false;
}

void yield(void) {
  register intptr_t a7 asm("a7") = 1;
  register intptr_t a0 asm("a0") = 0;
  register intptr_t a1 asm("a1") = 0;
  register intptr_t a2 asm("a2") = 0;
  asm volatile("ecall" : "+r"(a0) : "r"(a7), "r"(a1), "r"(a2) : "memory");
}

bool ienabled(void) {
  return false;
}

void iset(bool enable) {
  (void)enable;
}

Context *kcontext(Area kstack, void (*entry)(void *), void *arg) {
  (void)kstack;
  (void)entry;
  (void)arg;
  return 0;
}

bool vme_init(void *(*pgalloc)(int), void (*pgfree)(void *)) {
  (void)pgalloc;
  (void)pgfree;
  return false;
}

void protect(AddrSpace *as) {
  (void)as;
}

void unprotect(AddrSpace *as) {
  (void)as;
}

void map(AddrSpace *as, void *vaddr, void *paddr, int prot) {
  (void)as;
  (void)vaddr;
  (void)paddr;
  (void)prot;
}

Context *ucontext(AddrSpace *as, Area kstack, void *entry) {
  (void)as;
  (void)kstack;
  (void)entry;
  return 0;
}

bool mpe_init(void (*entry)()) {
  (void)entry;
  return true;
}

int cpu_count(void) {
  return 1;
}

int cpu_current(void) {
  return 0;
}

int atomic_xchg(int *addr, int newval) {
  int old;
  __atomic_exchange(addr, &newval, &old, __ATOMIC_SEQ_CST);
  return old;
}
