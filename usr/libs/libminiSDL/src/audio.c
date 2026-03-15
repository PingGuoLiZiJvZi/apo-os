#include <NDL.h>
#include <SDL.h>

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static SDL_AudioSpec g_audio_spec;
static int g_audio_opened = 0;
static int g_audio_paused = 1;
static uint8_t *g_stream_buf = NULL;

static uint16_t read_le16(FILE *fp) {
  uint8_t b[2] = {0};
  if (fread(b, 1, 2, fp) != 2) return 0;
  return (uint16_t)(b[0] | (b[1] << 8));
}

static uint32_t read_le32(FILE *fp) {
  uint8_t b[4] = {0};
  if (fread(b, 1, 4, fp) != 4) return 0;
  return (uint32_t)b[0] |
         ((uint32_t)b[1] << 8) |
         ((uint32_t)b[2] << 16) |
         ((uint32_t)b[3] << 24);
}

void SDL_UpdateAudio(void) {
  if (!g_audio_opened || g_audio_paused || !g_audio_spec.callback) return;
  if (g_audio_spec.size == 0) return;

  if (!g_stream_buf) {
    g_stream_buf = (uint8_t *)malloc(g_audio_spec.size);
    if (!g_stream_buf) return;
  }

  int pending = NDL_QueryAudio();
  while (pending < (int)(g_audio_spec.size * 2)) {
    memset(g_stream_buf, 0, g_audio_spec.size);
    g_audio_spec.callback(g_audio_spec.userdata, g_stream_buf, (int)g_audio_spec.size);
    int wrote = NDL_PlayAudio(g_stream_buf, (int)g_audio_spec.size);
    if (wrote <= 0) break;
    pending += wrote;
  }
}

int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained) {
  if (!desired) return -1;
  g_audio_spec = *desired;
  if (g_audio_spec.freq <= 0) g_audio_spec.freq = 44100;
  if (g_audio_spec.channels == 0) g_audio_spec.channels = 2;
  if (g_audio_spec.samples == 0) g_audio_spec.samples = 1024;
  g_audio_spec.size = (uint32_t)g_audio_spec.samples * (uint32_t)g_audio_spec.channels * 2;

  NDL_OpenAudio(g_audio_spec.freq, g_audio_spec.channels, g_audio_spec.samples);
  g_audio_opened = 1;
  g_audio_paused = 1;
  if (g_stream_buf) {
    free(g_stream_buf);
    g_stream_buf = NULL;
  }

  if (obtained) *obtained = g_audio_spec;
  return 0;
}

void SDL_CloseAudio(void) {
  g_audio_opened = 0;
  g_audio_paused = 1;
  if (g_stream_buf) {
    free(g_stream_buf);
    g_stream_buf = NULL;
  }
  NDL_CloseAudio();
}

void SDL_PauseAudio(int pause_on) {
  g_audio_paused = pause_on ? 1 : 0;
  if (!g_audio_paused) {
    SDL_UpdateAudio();
  }
}

SDL_AudioSpec *SDL_LoadWAV(const char *file, SDL_AudioSpec *spec, uint8_t **audio_buf, uint32_t *audio_len) {
  if (!file || !spec || !audio_buf || !audio_len) return NULL;

  FILE *fp = fopen(file, "rb");
  if (!fp) return NULL;

  char id[4] = {0};
  if (fread(id, 1, 4, fp) != 4 || memcmp(id, "RIFF", 4) != 0) {
    fclose(fp);
    return NULL;
  }
  (void)read_le32(fp);
  if (fread(id, 1, 4, fp) != 4 || memcmp(id, "WAVE", 4) != 0) {
    fclose(fp);
    return NULL;
  }

  int got_fmt = 0;
  int got_data = 0;
  uint16_t audio_format = 1;
  uint16_t channels = 0;
  uint32_t sample_rate = 0;
  uint16_t bits_per_sample = 0;
  uint8_t *data = NULL;
  uint32_t data_len = 0;

  while (!got_data) {
    if (fread(id, 1, 4, fp) != 4) break;
    uint32_t chunk_size = read_le32(fp);

    if (memcmp(id, "fmt ", 4) == 0) {
      audio_format = read_le16(fp);
      channels = read_le16(fp);
      sample_rate = read_le32(fp);
      (void)read_le32(fp);
      (void)read_le16(fp);
      bits_per_sample = read_le16(fp);
      if (chunk_size > 16) {
        fseek(fp, (long)(chunk_size - 16), SEEK_CUR);
      }
      got_fmt = 1;
    } else if (memcmp(id, "data", 4) == 0) {
      data = (uint8_t *)malloc(chunk_size);
      if (!data) {
        fclose(fp);
        return NULL;
      }
      if (fread(data, 1, chunk_size, fp) != chunk_size) {
        free(data);
        fclose(fp);
        return NULL;
      }
      data_len = chunk_size;
      got_data = 1;
    } else {
      fseek(fp, (long)chunk_size, SEEK_CUR);
    }

    if (chunk_size & 1) {
      fseek(fp, 1, SEEK_CUR);
    }
  }

  fclose(fp);

  if (!got_fmt || !got_data || audio_format != 1 || channels == 0 || sample_rate == 0) {
    free(data);
    return NULL;
  }

  memset(spec, 0, sizeof(*spec));
  spec->freq = (int)sample_rate;
  spec->channels = (uint8_t)channels;
  if (bits_per_sample == 8) {
    spec->format = AUDIO_U8;
  } else if (bits_per_sample == 16) {
    spec->format = AUDIO_S16SYS;
  } else {
    free(data);
    return NULL;
  }

  *audio_buf = data;
  *audio_len = data_len;
  return spec;
}

void SDL_FreeWAV(uint8_t *audio_buf) {
  free(audio_buf);
}

void SDL_MixAudio(uint8_t *dst, uint8_t *src, uint32_t len, int volume) {
  if (!dst || !src) return;
  if (volume <= 0) return;
  if (volume > SDL_MIX_MAXVOLUME) volume = SDL_MIX_MAXVOLUME;

  if (g_audio_spec.format == AUDIO_S16SYS) {
    int sample_cnt = (int)(len / 2);
    int16_t *d = (int16_t *)dst;
    int16_t *s = (int16_t *)src;
    for (int i = 0; i < sample_cnt; i++) {
      int mixed = d[i] + (s[i] * volume) / SDL_MIX_MAXVOLUME;
      if (mixed > 32767) mixed = 32767;
      if (mixed < -32768) mixed = -32768;
      d[i] = (int16_t)mixed;
    }
  } else {
    for (uint32_t i = 0; i < len; i++) {
      int mixed = (int)dst[i] + ((int)src[i] * volume) / SDL_MIX_MAXVOLUME;
      if (mixed > 255) mixed = 255;
      if (mixed < 0) mixed = 0;
      dst[i] = (uint8_t)mixed;
    }
  }
}

void SDL_LockAudio(void) {
}

void SDL_UnlockAudio(void) {
}
