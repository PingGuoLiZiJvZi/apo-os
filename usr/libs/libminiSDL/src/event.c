#include <NDL.h>
#include <SDL.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t key_state[512];

static int linux_code_to_sdl(int code) {
  switch (code) {
    case 1: return SDLK_ESCAPE;
    case 2: return SDLK_1;
    case 3: return SDLK_2;
    case 4: return SDLK_3;
    case 5: return SDLK_4;
    case 6: return SDLK_5;
    case 7: return SDLK_6;
    case 8: return SDLK_7;
    case 9: return SDLK_8;
    case 10: return SDLK_9;
    case 11: return SDLK_0;
    case 12: return SDLK_MINUS;
    case 13: return SDLK_EQUALS;
    case 14: return SDLK_BACKSPACE;
    case 15: return SDLK_TAB;
    case 16: return SDLK_Q;
    case 17: return SDLK_W;
    case 18: return SDLK_E;
    case 19: return SDLK_R;
    case 20: return SDLK_T;
    case 21: return SDLK_Y;
    case 22: return SDLK_U;
    case 23: return SDLK_I;
    case 24: return SDLK_O;
    case 25: return SDLK_P;
    case 26: return SDLK_LEFTBRACKET;
    case 27: return SDLK_RIGHTBRACKET;
    case 28: return SDLK_RETURN;
    case 29: return SDLK_LCTRL;
    case 30: return SDLK_A;
    case 31: return SDLK_S;
    case 32: return SDLK_D;
    case 33: return SDLK_F;
    case 34: return SDLK_G;
    case 35: return SDLK_H;
    case 36: return SDLK_J;
    case 37: return SDLK_K;
    case 38: return SDLK_L;
    case 39: return SDLK_SEMICOLON;
    case 40: return SDLK_APOSTROPHE;
    case 41: return SDLK_GRAVE;
    case 42: return SDLK_LSHIFT;
    case 43: return SDLK_BACKSLASH;
    case 44: return SDLK_Z;
    case 45: return SDLK_X;
    case 46: return SDLK_C;
    case 47: return SDLK_V;
    case 48: return SDLK_B;
    case 49: return SDLK_N;
    case 50: return SDLK_M;
    case 51: return SDLK_COMMA;
    case 52: return SDLK_PERIOD;
    case 53: return SDLK_SLASH;
    case 54: return SDLK_RSHIFT;
    case 56: return SDLK_LALT;
    case 57: return SDLK_SPACE;
    case 58: return SDLK_CAPSLOCK;
    case 59: return SDLK_F1;
    case 60: return SDLK_F2;
    case 61: return SDLK_F3;
    case 62: return SDLK_F4;
    case 63: return SDLK_F5;
    case 64: return SDLK_F6;
    case 65: return SDLK_F7;
    case 66: return SDLK_F8;
    case 67: return SDLK_F9;
    case 68: return SDLK_F10;
    case 87: return SDLK_F11;
    case 88: return SDLK_F12;
    case 97: return SDLK_RCTRL;
    case 100: return SDLK_RALT;
    case 102: return SDLK_HOME;
    case 103: return SDLK_UP;
    case 104: return SDLK_PAGEUP;
    case 105: return SDLK_LEFT;
    case 106: return SDLK_RIGHT;
    case 107: return SDLK_END;
    case 108: return SDLK_DOWN;
    case 109: return SDLK_PAGEDOWN;
    case 110: return SDLK_INSERT;
    case 111: return SDLK_DELETE;
    case 117: return SDLK_APPLICATION;
    default: return SDLK_NONE;
  }
}

static int parse_event(SDL_Event *ev, const char *buf) {
  int code = 0;
  if (sscanf(buf, "kd %d", &code) == 1) {
    int sym = linux_code_to_sdl(code);
    ev->type = SDL_KEYDOWN;
    ev->key.keysym.sym = (uint8_t)sym;
    if (sym >= 0 && sym < (int)sizeof(key_state)) key_state[sym] = 1;
    return 1;
  }
  if (sscanf(buf, "ku %d", &code) == 1) {
    int sym = linux_code_to_sdl(code);
    ev->type = SDL_KEYUP;
    ev->key.keysym.sym = (uint8_t)sym;
    if (sym >= 0 && sym < (int)sizeof(key_state)) key_state[sym] = 0;
    return 1;
  }
  return 0;
}

int SDL_PushEvent(SDL_Event *ev) {
  (void)ev;
  return 0;
}

int SDL_PollEvent(SDL_Event *ev) {
  if (!ev) return 0;
  SDL_UpdateAudio();
  char buf[64] = {0};
  int n = NDL_PollEvent(buf, sizeof(buf));
  if (n <= 0) {
    ev->type = SDL_KEYUP;
    ev->key.keysym.sym = SDLK_NONE;
    return 0;
  }
  return parse_event(ev, buf);
}

int SDL_WaitEvent(SDL_Event *ev) {
  if (!ev) return 0;
  char buf[64] = {0};
  int n = 0;
  do {
    SDL_UpdateAudio();
    n = NDL_PollEvent(buf, sizeof(buf));
  } while (n <= 0);
  return parse_event(ev, buf);
}

int SDL_PeepEvents(SDL_Event *ev, int numevents, int action, uint32_t mask) {
  (void)ev;
  (void)numevents;
  (void)action;
  (void)mask;
  return 0;
}

uint8_t *SDL_GetKeyState(int *numkeys) {
  if (numkeys) *numkeys = sizeof(key_state);
  return key_state;
}
