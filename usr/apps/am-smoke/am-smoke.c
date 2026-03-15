#include <am.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void play_beep(int hz, int ms) {
  if (hz <= 0 || ms <= 0) return;

  AM_AUDIO_CTRL_T ctrl = {
    .freq = 44100,
    .channels = 2,
    .samples = 1024,
  };
  ioe_write(AM_AUDIO_CTRL, &ctrl);

  int sample_rate = ctrl.freq;
  int frames = sample_rate * ms / 1000;
  if (frames <= 0) return;

  int16_t pcm[512 * 2];
  int half_period = sample_rate / (hz * 2);
  if (half_period < 1) half_period = 1;

  int generated = 0;
  int phase_count = 0;
  while (generated < frames) {
    int chunk = frames - generated;
    if (chunk > 512) chunk = 512;

    for (int i = 0; i < chunk; i++) {
      int16_t v = (phase_count < half_period) ? 7000 : -7000;
      pcm[i * 2] = v;
      pcm[i * 2 + 1] = v;
      phase_count++;
      if (phase_count >= half_period * 2) phase_count = 0;
    }

    AM_AUDIO_PLAY_T play = {
      .buf = (Area){ .start = pcm, .end = pcm + chunk * 2 },
    };
    ioe_write(AM_AUDIO_PLAY, &play);
    generated += chunk;
  }
}

int main() {
  ioe_init();

  AM_GPU_CONFIG_T gpu;
  ioe_read(AM_GPU_CONFIG, &gpu);
  printf("[am-smoke] gpu present=%d size=%dx%d\n", gpu.present, gpu.width, gpu.height);

  AM_TIMER_UPTIME_T t0, t1;
  ioe_read(AM_TIMER_UPTIME, &t0);

  AM_INPUT_CONFIG_T icfg;
  ioe_read(AM_INPUT_CONFIG, &icfg);
  printf("[am-smoke] input present=%d\n", icfg.present);

  AM_AUDIO_CONFIG_T acfg;
  ioe_read(AM_AUDIO_CONFIG, &acfg);
  printf("[am-smoke] audio present=%d bufsize=%d\n", acfg.present, acfg.bufsize);
  if (acfg.present) {
    printf("[am-smoke] audio test: startup beep\n");
    play_beep(660, 120);
  }

  if (gpu.present && gpu.width > 0 && gpu.height > 0) {
    uint32_t row[64];
    for (int i = 0; i < 64; i++) row[i] = 0x0000a0ff;
    AM_GPU_FBDRAW_T draw = {
      .x = 10,
      .y = 10,
      .pixels = row,
      .w = 64,
      .h = 1,
      .sync = 1,
    };
    for (int y = 10; y < 42; y++) {
      draw.y = y;
      ioe_write(AM_GPU_FBDRAW, &draw);
    }
  }

  printf("[am-smoke] press ESC to exit\n");
  while (1) {
    AM_INPUT_KEYBRD_T kbd;
    ioe_read(AM_INPUT_KEYBRD, &kbd);
    if (kbd.keycode != AM_KEY_NONE) {
      printf("[am-smoke] key %s code=%d\n", kbd.keydown ? "down" : "up", kbd.keycode);
      if (kbd.keydown && kbd.keycode == AM_KEY_ESCAPE) break;
    }

    ioe_read(AM_TIMER_UPTIME, &t1);
    if (t1.us - t0.us > 1000000ULL) {
      AM_AUDIO_STATUS_T ast;
      ioe_read(AM_AUDIO_STATUS, &ast);
      printf("[am-smoke] audio count=%d\n", ast.count);
      printf("[am-smoke] uptime +1s\n");
      t0 = t1;
    }
  }

  return 0;
}
