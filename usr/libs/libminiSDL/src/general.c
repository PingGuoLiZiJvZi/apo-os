#include <NDL.h>
#include <SDL.h>

#include <stdarg.h>
#include <stdio.h>

static char sdl_error_buf[256];

int SDL_Init(uint32_t flags) {
  return NDL_Init(flags);
}

void SDL_Quit(void) {
  NDL_Quit();
}

char *SDL_GetError(void) {
  return sdl_error_buf;
}

int SDL_SetError(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(sdl_error_buf, sizeof(sdl_error_buf), fmt, ap);
  va_end(ap);
  return -1;
}

int SDL_ShowCursor(int toggle) {
  return toggle;
}

void SDL_WM_SetCaption(const char *title, const char *icon) {
  (void)title;
  (void)icon;
}
