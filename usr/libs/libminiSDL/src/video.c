#include <NDL.h>
#include <SDL.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static inline int mask_to_shift(uint32_t mask) {
  switch (mask) {
    case 0x000000ff: return 0;
    case 0x0000ff00: return 8;
    case 0x00ff0000: return 16;
    case 0xff000000: return 24;
    default: return 0;
  }
}

SDL_Surface *SDL_CreateRGBSurface(uint32_t flags, int width, int height, int depth,
    uint32_t Rmask, uint32_t Gmask, uint32_t Bmask, uint32_t Amask) {
  assert(depth == 8 || depth == 32);
  SDL_Surface *s = (SDL_Surface *)malloc(sizeof(SDL_Surface));
  if (!s) return NULL;
  memset(s, 0, sizeof(*s));

  SDL_PixelFormat *fmt = (SDL_PixelFormat *)malloc(sizeof(SDL_PixelFormat));
  if (!fmt) { free(s); return NULL; }
  memset(fmt, 0, sizeof(*fmt));

  if (depth == 8) {
    fmt->palette = (SDL_Palette *)malloc(sizeof(SDL_Palette));
    if (!fmt->palette) { free(fmt); free(s); return NULL; }
    fmt->palette->colors = (SDL_Color *)malloc(sizeof(SDL_Color) * 256);
    if (!fmt->palette->colors) {
      free(fmt->palette);
      free(fmt);
      free(s);
      return NULL;
    }
    memset(fmt->palette->colors, 0, sizeof(SDL_Color) * 256);
    fmt->palette->ncolors = 256;
  } else {
    fmt->Rmask = Rmask;
    fmt->Gmask = Gmask;
    fmt->Bmask = Bmask;
    fmt->Amask = Amask;
    fmt->Rshift = mask_to_shift(Rmask);
    fmt->Gshift = mask_to_shift(Gmask);
    fmt->Bshift = mask_to_shift(Bmask);
    fmt->Ashift = mask_to_shift(Amask);
  }

  fmt->BitsPerPixel = (uint8_t)depth;
  fmt->BytesPerPixel = (uint8_t)(depth / 8);

  s->flags = flags;
  s->format = fmt;
  s->w = width;
  s->h = height;
  s->pitch = (uint16_t)(width * fmt->BytesPerPixel);

  if (!(flags & SDL_PREALLOC)) {
    s->pixels = (uint8_t *)malloc((size_t)s->pitch * (size_t)height);
    if (!s->pixels) {
      SDL_FreeSurface(s);
      return NULL;
    }
  }
  return s;
}

SDL_Surface *SDL_CreateRGBSurfaceFrom(void *pixels, int width, int height, int depth,
    int pitch, uint32_t Rmask, uint32_t Gmask, uint32_t Bmask, uint32_t Amask) {
  SDL_Surface *s = SDL_CreateRGBSurface(SDL_PREALLOC, width, height, depth, Rmask, Gmask, Bmask, Amask);
  if (!s) return NULL;
  s->pitch = (uint16_t)pitch;
  s->pixels = (uint8_t *)pixels;
  return s;
}

SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, uint32_t flags) {
  if (flags & SDL_HWSURFACE) {
    NDL_OpenCanvas(&width, &height);
  }
  return SDL_CreateRGBSurface(flags, width, height, bpp, DEFAULT_RMASK, DEFAULT_GMASK, DEFAULT_BMASK, DEFAULT_AMASK);
}

void SDL_FreeSurface(SDL_Surface *s) {
  if (!s) return;
  if (s->format) {
    if (s->format->palette) {
      free(s->format->palette->colors);
      free(s->format->palette);
    }
    free(s->format);
  }
  if (s->pixels && !(s->flags & SDL_PREALLOC)) free(s->pixels);
  free(s);
}

void SDL_BlitSurface(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect) {
  assert(src && dst);
  int bpp = src->format->BytesPerPixel;
  int sx = srcrect ? srcrect->x : 0;
  int sy = srcrect ? srcrect->y : 0;
  int sw = srcrect ? srcrect->w : src->w;
  int sh = srcrect ? srcrect->h : src->h;
  int dx = dstrect ? dstrect->x : 0;
  int dy = dstrect ? dstrect->y : 0;

  for (int y = 0; y < sh; y++) {
    if (sy + y < 0 || sy + y >= src->h || dy + y < 0 || dy + y >= dst->h) continue;
    uint8_t *srow = src->pixels + (sy + y) * src->pitch + sx * bpp;
    uint8_t *drow = dst->pixels + (dy + y) * dst->pitch + dx * bpp;
    memcpy(drow, srow, (size_t)sw * bpp);
  }
}

void SDL_FillRect(SDL_Surface *dst, SDL_Rect *dstrect, uint32_t color) {
  if (!dst || !dst->pixels) return;
  int x = dstrect ? dstrect->x : 0;
  int y = dstrect ? dstrect->y : 0;
  int w = dstrect ? dstrect->w : dst->w;
  int h = dstrect ? dstrect->h : dst->h;

  if (dst->format->BitsPerPixel == 8) {
    uint8_t c = (uint8_t)color;
    for (int j = 0; j < h; j++) {
      memset(dst->pixels + (y + j) * dst->pitch + x, c, (size_t)w);
    }
  } else {
    for (int j = 0; j < h; j++) {
      uint32_t *row = (uint32_t *)(dst->pixels + (y + j) * dst->pitch);
      for (int i = 0; i < w; i++) row[x + i] = color;
    }
  }
}

void SDL_UpdateRect(SDL_Surface *s, int x, int y, int w, int h) {
  if (!s || !s->pixels) return;
  if (w <= 0 || h <= 0) {
    x = 0; y = 0; w = s->w; h = s->h;
  }

  if (s->format->BitsPerPixel == 32) {
    for (int row = 0; row < h; row++) {
      uint32_t *line = (uint32_t *)(s->pixels + (y + row) * s->pitch) + x;
      NDL_DrawRect(line, x, y + row, w, 1);
    }
  } else {
    uint32_t *tmp = (uint32_t *)malloc((size_t)w * h * sizeof(uint32_t));
    if (!tmp) return;
    SDL_Palette *p = s->format->palette;
    for (int j = 0; j < h; j++) {
      uint8_t *src = s->pixels + (y + j) * s->pitch + x;
      for (int i = 0; i < w; i++) {
        tmp[j * w + i] = p->colors[src[i]].val;
      }
    }
    NDL_DrawRect(tmp, x, y, w, h);
    free(tmp);
  }
}

void SDL_SoftStretch(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect) {
  SDL_BlitSurface(src, srcrect, dst, dstrect);
}

void SDL_SetPalette(SDL_Surface *s, int flags, SDL_Color *colors, int firstcolor, int ncolors) {
  (void)flags;
  if (!s || !s->format || !s->format->palette || !colors) return;
  for (int i = 0; i < ncolors; i++) {
    int idx = firstcolor + i;
    if (idx < 0 || idx >= s->format->palette->ncolors) continue;
    s->format->palette->colors[idx] = colors[i];
  }
}

SDL_Surface *SDL_ConvertSurface(SDL_Surface *src, SDL_PixelFormat *fmt, uint32_t flags) {
  (void)flags;
  if (!src || !fmt) return NULL;
  SDL_Surface *dst = SDL_CreateRGBSurface(0, src->w, src->h, fmt->BitsPerPixel, fmt->Rmask, fmt->Gmask, fmt->Bmask, fmt->Amask);
  if (!dst) return NULL;
  SDL_BlitSurface(src, NULL, dst, NULL);
  return dst;
}

uint32_t SDL_MapRGBA(SDL_PixelFormat *fmt, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  if (!fmt) return 0;
  if (fmt->BitsPerPixel == 8) return r;
  return ((uint32_t)r << fmt->Rshift) | ((uint32_t)g << fmt->Gshift) |
         ((uint32_t)b << fmt->Bshift) | ((uint32_t)a << fmt->Ashift);
}

int SDL_LockSurface(SDL_Surface *s) {
  (void)s;
  return 0;
}

void SDL_UnlockSurface(SDL_Surface *s) {
  (void)s;
}
