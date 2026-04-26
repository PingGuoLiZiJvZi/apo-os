#include <NDL.h>
#include <SDL.h>

#include <sys/time.h>
#include <stdint.h>

static void sdl_yield(void) {
  register intptr_t a7 asm("a7") = 1;
  register intptr_t a0 asm("a0") = 0;
  register intptr_t a1 asm("a1") = 0;
  register intptr_t a2 asm("a2") = 0;
  asm volatile("ecall" : "+r"(a0) : "r"(a7), "r"(a1), "r"(a2) : "memory");
}

SDL_TimerID SDL_AddTimer(uint32_t interval, SDL_NewTimerCallback callback, void *param) {
  (void)interval;
  (void)callback;
  (void)param;
  return 0;
}

int SDL_RemoveTimer(SDL_TimerID id) {
  (void)id;
  return 1;
}

uint32_t SDL_GetTicks(void) {
  return NDL_GetTicks();
}

void SDL_Delay(uint32_t ms) {
  uint32_t start = SDL_GetTicks();
  while (SDL_GetTicks() - start < ms) {
    SDL_UpdateAudio();
    sdl_yield();
  }
}
