#include <SDL.h>
#include <stdio.h>

static int clampi(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

int main(void) {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    printf("[sdl-ipc-smoke] SDL_Init failed\n");
    return 1;
  }

  int w = 320;
  int h = 240;
  SDL_Surface *screen = SDL_SetVideoMode(w, h, 32, SDL_HWSURFACE);
  if (!screen) {
    printf("[sdl-ipc-smoke] SDL_SetVideoMode failed\n");
    return 1;
  }

  int box_x = w / 2 - 15;
  int box_y = h / 2 - 15;
  int running = 1;
  int frame = 0;

  printf("[sdl-ipc-smoke] started, arrow keys move box, ESC exits\n");

  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_KEYDOWN) {
        printf("[sdl-ipc-smoke] keydown sym=%d\n", ev.key.keysym.sym);
        switch (ev.key.keysym.sym) {
          case SDLK_LEFT:  box_x -= 5; break;
          case SDLK_RIGHT: box_x += 5; break;
          case SDLK_UP:    box_y -= 5; break;
          case SDLK_DOWN:  box_y += 5; break;
          case SDLK_ESCAPE: running = 0; break;
          default: break;
        }
      } else if (ev.type == SDL_KEYUP) {
        printf("[sdl-ipc-smoke] keyup sym=%d\n", ev.key.keysym.sym);
      }
    }

    box_x = clampi(box_x, 0, w - 30);
    box_y = clampi(box_y, 0, h - 30);

    SDL_FillRect(screen, NULL, 0x00182038);
    SDL_Rect r;
    r.x = (int16_t)box_x;
    r.y = (int16_t)box_y;
    r.w = 30;
    r.h = 30;
    SDL_FillRect(screen, &r, 0x00ffd166);

    SDL_UpdateRect(screen, 0, 0, 0, 0);
    if ((frame % 60) == 0) {
      printf("[sdl-ipc-smoke] frame=%d box=(%d,%d)\n", frame, box_x, box_y);
    }
    frame++;
    SDL_Delay(16);
  }

  SDL_Quit();
  return 0;
}
