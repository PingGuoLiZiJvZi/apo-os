#include <NDL.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  int sample_rate;
  int channels;
  int bits_per_sample;
  uint8_t *data;
  uint32_t data_len;
} WavPcm;

static uint16_t read_le16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int read_full(int fd, void *buf, int len) {
  uint8_t *p = (uint8_t *)buf;
  int off = 0;
  while (off < len) {
    int n = read(fd, p + off, (size_t)(len - off));
    if (n <= 0) return -1;
    off += n;
  }
  return 0;
}

static int load_wav_pcm16(const char *path, WavPcm *out) {
  memset(out, 0, sizeof(*out));

  int fd = open(path, O_RDONLY, 0);
  if (fd < 0) {
    return -1;
  }

  uint8_t riff_hdr[12];
  if (read_full(fd, riff_hdr, sizeof(riff_hdr)) < 0) {
    close(fd);
    return -2;
  }

  if (memcmp(riff_hdr, "RIFF", 4) != 0 || memcmp(riff_hdr + 8, "WAVE", 4) != 0) {
    close(fd);
    return -3;
  }

  int fmt_ok = 0;
  int data_ok = 0;
  uint16_t audio_format = 0;

  while (!(fmt_ok && data_ok)) {
    uint8_t ck_hdr[8];
    if (read_full(fd, ck_hdr, sizeof(ck_hdr)) < 0) break;

    uint32_t ck_size = read_le32(ck_hdr + 4);

    if (memcmp(ck_hdr, "fmt ", 4) == 0) {
      if (ck_size < 16) {
        close(fd);
        return -4;
      }
      uint8_t *fmt = (uint8_t *)malloc(ck_size);
      if (!fmt) {
        close(fd);
        return -5;
      }
      if (read_full(fd, fmt, (int)ck_size) < 0) {
        free(fmt);
        close(fd);
        return -6;
      }
      audio_format = read_le16(fmt + 0);
      out->channels = (int)read_le16(fmt + 2);
      out->sample_rate = (int)read_le32(fmt + 4);
      out->bits_per_sample = (int)read_le16(fmt + 14);
      free(fmt);
      fmt_ok = 1;
    } else if (memcmp(ck_hdr, "data", 4) == 0) {
      out->data = (uint8_t *)malloc(ck_size);
      if (!out->data) {
        close(fd);
        return -7;
      }
      if (read_full(fd, out->data, (int)ck_size) < 0) {
        free(out->data);
        out->data = NULL;
        close(fd);
        return -8;
      }
      out->data_len = ck_size;
      data_ok = 1;
    } else {
      off_t skipped = lseek(fd, (off_t)ck_size, SEEK_CUR);
      if (skipped < 0) {
        close(fd);
        return -9;
      }
    }

    if (ck_size & 1) {
      if (lseek(fd, 1, SEEK_CUR) < 0) {
        close(fd);
        return -10;
      }
    }
  }

  close(fd);

  if (!(fmt_ok && data_ok)) {
    free(out->data);
    memset(out, 0, sizeof(*out));
    return -11;
  }

  if (audio_format != 1) {
    free(out->data);
    memset(out, 0, sizeof(*out));
    return -12;
  }

  return 0;
}

static void delay_ms(uint32_t ms) {
  uint32_t start = NDL_GetTicks();
  while ((uint32_t)(NDL_GetTicks() - start) < ms) {
  }
}

static int16_t read_s16_sample(const uint8_t *base, uint32_t frame_idx, int channels, int bits, int ch) {
  if (bits == 16) {
    uint32_t off = (frame_idx * (uint32_t)channels + (uint32_t)ch) * 2u;
    uint16_t lo = base[off + 0];
    uint16_t hi = base[off + 1];
    return (int16_t)(lo | (hi << 8));
  }
  if (bits == 8) {
    uint32_t off = frame_idx * (uint32_t)channels + (uint32_t)ch;
    int v = (int)base[off] - 128;
    return (int16_t)(v << 8);
  }
  return 0;
}

static int play_pcm(const WavPcm *wav) {
  if (wav->channels <= 0 || wav->sample_rate <= 0 || wav->data_len == 0 || wav->data == NULL) {
    return -2;
  }
  if (!(wav->bits_per_sample == 8 || wav->bits_per_sample == 16)) {
    printf("[audio-smoke] unsupported bits_per_sample=%d (only 8/16 PCM)\n", wav->bits_per_sample);
    return -1;
  }

  int audiofd = open("/device/audio", O_WRONLY, 0);
  if (audiofd < 0) {
    printf("[audio-smoke] open /device/audio failed errno=%d\n", errno);
    return -3;
  }

  const int dst_rate = 44100;
  const int src_channels = wav->channels;
  const int src_bits = wav->bits_per_sample;
  const uint32_t src_frame_size = (uint32_t)src_channels * (uint32_t)(src_bits / 8);
  if (src_frame_size == 0) {
    close(audiofd);
    return -4;
  }
  const uint32_t src_frames = wav->data_len / src_frame_size;
  if (src_frames == 0) {
    close(audiofd);
    return -5;
  }

  uint32_t out_frames_total = (uint32_t)(((uint64_t)src_frames * (uint64_t)dst_rate) / (uint64_t)wav->sample_rate);
  if (out_frames_total == 0) out_frames_total = 1;

  const uint32_t chunk_frames = 1024;
  int16_t outbuf[1024 * 2];
  uint32_t out_done = 0;

  while (out_done < out_frames_total) {
    uint32_t frames = out_frames_total - out_done;
    if (frames > chunk_frames) frames = chunk_frames;

    for (uint32_t i = 0; i < frames; i++) {
      uint32_t out_idx = out_done + i;
      uint32_t src_idx = (uint32_t)(((uint64_t)out_idx * (uint64_t)wav->sample_rate) / (uint64_t)dst_rate);
      if (src_idx >= src_frames) src_idx = src_frames - 1;

      int16_t l = 0;
      int16_t r = 0;
      if (src_channels == 1) {
        int16_t m = read_s16_sample(wav->data, src_idx, src_channels, src_bits, 0);
        l = m;
        r = m;
      } else {
        l = read_s16_sample(wav->data, src_idx, src_channels, src_bits, 0);
        r = read_s16_sample(wav->data, src_idx, src_channels, src_bits, 1);
      }

      outbuf[i * 2 + 0] = l;
      outbuf[i * 2 + 1] = r;
    }

    const uint8_t *p = (const uint8_t *)outbuf;
    int bytes = (int)(frames * 2u * sizeof(int16_t));
    int sent = 0;
    while (sent < bytes) {
      int n = write(audiofd, p + sent, (size_t)(bytes - sent));
      if (n <= 0) {
        close(audiofd);
        return -6;
      }
      sent += n;
    }

    out_done += frames;
  }

  close(audiofd);

  // 给设备一点时间播放尾部缓冲。
  delay_ms(120);

  return 0;
}

int main(void) {
  if (NDL_Init(0) != 0) {
    printf("[audio-smoke] NDL_Init failed\n");
    return 1;
  }

  const char *candidates[] = {
    "/share/games/bird/sfx_hit.wav",
    "/home/pglzjz/apo-os/usr/disk/share/games/bird/sfx_hit.wav",
  };

  WavPcm wav;
  int ok = -1;
  const char *selected = NULL;
  for (int i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
    int rc = load_wav_pcm16(candidates[i], &wav);
    if (rc == 0) {
      selected = candidates[i];
      ok = 0;
      break;
    }
    printf("[audio-smoke] open/parse failed: %s (rc=%d errno=%d)\n", candidates[i], rc, errno);
  }

  if (ok != 0) {
    printf("[audio-smoke] cannot load target wav\n");
    NDL_Quit();
    return 2;
  }

  printf("[audio-smoke] loaded: %s\n", selected);
  printf("[audio-smoke] rate=%d channels=%d bits=%d bytes=%u\n",
         wav.sample_rate, wav.channels, wav.bits_per_sample, wav.data_len);
  printf("[audio-smoke] start playback...\n");

  int prc = play_pcm(&wav);
  if (prc == 0) {
    printf("[audio-smoke] playback done\n");
  } else {
    printf("[audio-smoke] playback failed rc=%d\n", prc);
  }

  free(wav.data);
  NDL_Quit();
  return (prc == 0) ? 0 : 3;
}
