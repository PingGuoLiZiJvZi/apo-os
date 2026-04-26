#include "desktop.h"

#define BOOT_TEXT "Welcome to APO-OS!"
#define BOOT_TEXT_MS 4000u
#define BOOT_TEXT_TYPE_MS 3600u
#define BOOT_STEP_MS 16u
#define BOOT_AUDIO_CHUNK 4096
#define BOOT_AUDIO_WATERMARK 8192

#define BOOT_NYAN_PATH "/share/boot/nyan_loop.nyan"
#define BOOT_AUDIO_INTRO_PATH "/share/boot/nyan_audio_intro.wav"
#define BOOT_AUDIO_LOOP_PATH "/share/boot/nyan_audio_loop.wav"

#define NYAN_FORMAT_RGB565_RLE 1u
#define BOOT_MAX_SKIP_FRAMES 4u

typedef struct {
    int fd;
    int valid;
    uint32_t data_start;
    uint32_t data_size;
    uint32_t pos;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
} WavStream;

typedef struct {
    WavStream intro;
    WavStream loop;
    int active;
    int phase;
} BootAudio;

typedef struct {
    int fd;
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint32_t frame_count;
    uint32_t data_start;
    uint8_t *frame_buf;
    uint32_t frame_buf_size;
} NyanMovie;

static uint8_t audio_buf[BOOT_AUDIO_CHUNK];

static const char *const GLYPH_SPACE[7] = {
    "00000", "00000", "00000", "00000", "00000", "00000", "00000"
};

static const char *const GLYPH_W[7] = {
    "10001", "10001", "10001", "10101", "10101", "11011", "10001"
};

static const char *const GLYPH_e[7] = {
    "00000", "01110", "10001", "11111", "10000", "01110", "00000"
};

static const char *const GLYPH_l[7] = {
    "11000", "01000", "01000", "01000", "01000", "11100", "00000"
};

static const char *const GLYPH_c[7] = {
    "00000", "01110", "10000", "10000", "10000", "01110", "00000"
};

static const char *const GLYPH_o[7] = {
    "00000", "01110", "10001", "10001", "10001", "01110", "00000"
};

static const char *const GLYPH_m[7] = {
    "00000", "11010", "10101", "10101", "10101", "10101", "00000"
};

static const char *const GLYPH_t[7] = {
    "00100", "11111", "00100", "00100", "00100", "00011", "00000"
};

static const char *const GLYPH_A[7] = {
    "01110", "10001", "10001", "11111", "10001", "10001", "10001"
};

static const char *const GLYPH_P[7] = {
    "11110", "10001", "10001", "11110", "10000", "10000", "10000"
};

static const char *const GLYPH_O[7] = {
    "01110", "10001", "10001", "10001", "10001", "10001", "01110"
};

static const char *const GLYPH_S[7] = {
    "01111", "10000", "10000", "01110", "00001", "00001", "11110"
};

static const char *const GLYPH_DASH[7] = {
    "00000", "00000", "00000", "11111", "00000", "00000", "00000"
};

static const char *const GLYPH_BANG[7] = {
    "00100", "00100", "00100", "00100", "00100", "00000", "00100"
};

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int read_exact_fd(int fd, void *buf, int len) {
    uint8_t *p = (uint8_t *)buf;
    int off = 0;

    while (off < len) {
        int n = read(fd, p + off, (size_t)(len - off));
        if (n <= 0) {
            return 0;
        }
        off += n;
    }
    return 1;
}

static int skip_fd(int fd, uint32_t len) {
    if (len == 0) {
        return 1;
    }
    return lseek(fd, (off_t)len, SEEK_CUR) >= 0;
}

static int boot_stop_requested(void) {
    int key;
    int key_type;
    int ma_code;
    int ma_val;

    while (poll_events(&key, &key_type, &ma_code, &ma_val)) {
        if (key_type == 1) {
            return 1;
        }
    }
    return 0;
}

static void wav_close(WavStream *s) {
    if (s->fd >= 0) {
        close(s->fd);
    }
    memset(s, 0, sizeof(*s));
    s->fd = -1;
}

static int wav_rewind_data(WavStream *s) {
    if (s->fd < 0) {
        return 0;
    }
    if (lseek(s->fd, (off_t)s->data_start, SEEK_SET) < 0) {
        return 0;
    }
    s->pos = 0;
    return 1;
}

static int wav_open(WavStream *s, const char *path) {
    uint8_t hdr[12];
    int got_fmt = 0;
    int got_data = 0;

    memset(s, 0, sizeof(*s));
    s->fd = -1;

    s->fd = open(path, O_RDONLY, 0);
    if (s->fd < 0) {
        return 0;
    }

    if (!read_exact_fd(s->fd, hdr, sizeof(hdr)) ||
        memcmp(hdr, "RIFF", 4) != 0 ||
        memcmp(hdr + 8, "WAVE", 4) != 0) {
        wav_close(s);
        return 0;
    }

    while (1) {
        uint8_t chdr[8];
        uint32_t chunk_size;

        if (!read_exact_fd(s->fd, chdr, sizeof(chdr))) {
            break;
        }
        chunk_size = le32(chdr + 4);

        if (memcmp(chdr, "fmt ", 4) == 0) {
            uint8_t fmt[32];
            uint32_t take = chunk_size;
            uint16_t audio_format;

            if (take > sizeof(fmt)) {
                take = sizeof(fmt);
            }
            if (chunk_size < 16 || !read_exact_fd(s->fd, fmt, (int)take)) {
                wav_close(s);
                return 0;
            }
            if (!skip_fd(s->fd, chunk_size - take)) {
                wav_close(s);
                return 0;
            }
            audio_format = le16(fmt);
            s->channels = le16(fmt + 2);
            s->sample_rate = le32(fmt + 4);
            s->bits_per_sample = le16(fmt + 14);
            got_fmt = (audio_format == 1 && s->channels > 0 &&
                       s->sample_rate > 0 && s->bits_per_sample == 16);
        } else if (memcmp(chdr, "data", 4) == 0) {
            off_t pos = lseek(s->fd, 0, SEEK_CUR);
            if (pos < 0) {
                wav_close(s);
                return 0;
            }
            s->data_start = (uint32_t)pos;
            s->data_size = chunk_size;
            got_data = 1;
            if (!skip_fd(s->fd, chunk_size)) {
                wav_close(s);
                return 0;
            }
        } else {
            if (!skip_fd(s->fd, chunk_size)) {
                wav_close(s);
                return 0;
            }
        }

        if ((chunk_size & 1) && !skip_fd(s->fd, 1)) {
            wav_close(s);
            return 0;
        }
    }

    if (!got_fmt || !got_data || s->data_size == 0 || !wav_rewind_data(s)) {
        wav_close(s);
        return 0;
    }

    s->valid = 1;
    return 1;
}

static void boot_audio_start(BootAudio *audio) {
    WavStream *fmt = NULL;

    memset(audio, 0, sizeof(*audio));
    audio->intro.fd = -1;
    audio->loop.fd = -1;

    wav_open(&audio->intro, BOOT_AUDIO_INTRO_PATH);
    wav_open(&audio->loop, BOOT_AUDIO_LOOP_PATH);

    if (audio->intro.valid) {
        fmt = &audio->intro;
        audio->phase = 0;
    } else if (audio->loop.valid) {
        fmt = &audio->loop;
        audio->phase = 1;
    }

    if (!fmt) {
        return;
    }

    NDL_OpenAudio((int)fmt->sample_rate, (int)fmt->channels, 1024);
    audio->active = 1;
}

static WavStream *boot_audio_current(BootAudio *audio) {
    if (audio->phase == 0 && audio->intro.valid) {
        return &audio->intro;
    }
    if (audio->loop.valid) {
        audio->phase = 1;
        return &audio->loop;
    }
    return NULL;
}

static int boot_audio_advance(BootAudio *audio) {
    if (audio->phase == 0 && audio->loop.valid) {
        audio->phase = 1;
        return wav_rewind_data(&audio->loop);
    }
    if (audio->phase == 1 && audio->loop.valid) {
        return wav_rewind_data(&audio->loop);
    }
    audio->active = 0;
    return 0;
}

static void boot_audio_pump(BootAudio *audio) {
    if (!audio->active) {
        return;
    }

    while (NDL_QueryAudio() < BOOT_AUDIO_WATERMARK) {
        WavStream *s = boot_audio_current(audio);
        uint32_t left;
        int want;
        int n;

        if (!s || !s->valid) {
            audio->active = 0;
            break;
        }

        if (s->pos >= s->data_size) {
            if (!boot_audio_advance(audio)) {
                break;
            }
            continue;
        }

        left = s->data_size - s->pos;
        want = (left > BOOT_AUDIO_CHUNK) ? BOOT_AUDIO_CHUNK : (int)left;
        n = read(s->fd, audio_buf, (size_t)want);
        if (n <= 0) {
            if (!boot_audio_advance(audio)) {
                break;
            }
            continue;
        }

        s->pos += (uint32_t)n;
        if (NDL_PlayAudio(audio_buf, n) <= 0) {
            audio->active = 0;
            break;
        }
    }
}

static void boot_audio_stop(BootAudio *audio) {
    if (audio->active) {
        NDL_CloseAudio();
    }
    wav_close(&audio->intro);
    wav_close(&audio->loop);
    audio->active = 0;
}

static int wait_until(uint32_t deadline, BootAudio *audio) {
    while ((int32_t)(NDL_GetTicks() - deadline) < 0) {
        boot_audio_pump(audio);
        if (boot_stop_requested()) {
            return 1;
        }
        yield();
    }
    return 0;
}

static const char *const *glyph_for(char ch) {
    switch (ch) {
    case 'W': return GLYPH_W;
    case 'e': return GLYPH_e;
    case 'l': return GLYPH_l;
    case 'c': return GLYPH_c;
    case 'o': return GLYPH_o;
    case 'm': return GLYPH_m;
    case 't': return GLYPH_t;
    case 'A': return GLYPH_A;
    case 'P': return GLYPH_P;
    case 'O': return GLYPH_O;
    case 'S': return GLYPH_S;
    case '-': return GLYPH_DASH;
    case '!': return GLYPH_BANG;
    case ' ': return GLYPH_SPACE;
    default: return GLYPH_SPACE;
    }
}

static void draw_glyph(int x, int y, int scale, char ch, uint32_t color) {
    const char *const *g = glyph_for(ch);

    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 5; col++) {
            if (g[row][col] == '1') {
                fb_fill_rect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

static void draw_boot_text(int visible, int cursor_on) {
    const char *text = BOOT_TEXT;
    int len = (int)strlen(text);
    int scale = 4;
    int advance = 6 * scale;
    int text_w = len * advance - scale;
    int text_h = 7 * scale;
    int x = (screen_w - text_w) / 2;
    int y = (screen_h - text_h) / 2;
    Rect dirty;

    dirty.x = x - 2 * scale;
    dirty.y = y - 2 * scale;
    dirty.w = text_w + 6 * scale;
    dirty.h = text_h + 4 * scale;
    rect_clip(&dirty);

    fb_fill_rect(dirty.x, dirty.y, dirty.w, dirty.h, 0x00000000);
    for (int i = 0; i < visible && i < len; i++) {
        draw_glyph(x + i * advance, y, scale, text[i], 0x0040ff70);
    }

    if (cursor_on) {
        int cx = x + visible * advance;
        fb_fill_rect(cx, y, 3 * scale, text_h, 0x0040ff70);
    }

    flush_rect(dirty);
}

static int play_terminal_intro(BootAudio *audio) {
    uint32_t intro_start;
    int len = (int)strlen(BOOT_TEXT);
    int last_visible = -1;
    int last_cursor = -1;

    fb_fill_rect(0, 0, screen_w, screen_h, 0x00000000);
    NDL_FlushRect(0, 0, screen_w, screen_h);

    intro_start = NDL_GetTicks();
    while ((uint32_t)(NDL_GetTicks() - intro_start) < BOOT_TEXT_MS) {
        uint32_t elapsed = NDL_GetTicks() - intro_start;
        int visible = (int)((elapsed * (uint32_t)(len + 1)) / BOOT_TEXT_TYPE_MS);
        int cursor_on = ((elapsed / 320u) & 1u) == 0;

        if (visible > len) {
            visible = len;
        }

        if (visible != last_visible || cursor_on != last_cursor) {
            draw_boot_text(visible, cursor_on);
            last_visible = visible;
            last_cursor = cursor_on;
        }

        boot_audio_pump(audio);
        if (boot_stop_requested()) {
            return 1;
        }
        if (wait_until(NDL_GetTicks() + BOOT_STEP_MS, audio)) {
            return 1;
        }
    }

    return 0;
}

static uint32_t rgb565_to_xrgb(uint16_t c) {
    uint32_t r = (uint32_t)((c >> 11) & 0x1f);
    uint32_t g = (uint32_t)((c >> 5) & 0x3f);
    uint32_t b = (uint32_t)(c & 0x1f);

    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
    return (r << 16) | (g << 8) | b;
}

static void draw_run(uint32_t *row, uint32_t *col, uint32_t count,
                     uint32_t color, const NyanMovie *movie,
                     int dst_x, int dst_y) {
    uint32_t left = count;

    while (left > 0) {
        uint32_t span = movie->width - *col;
        int y = dst_y + (int)*row;
        int x = dst_x + (int)*col;

        if (span > left) {
            span = left;
        }

        if (y >= 0 && y < screen_h) {
            int draw_x = x;
            int draw_w = (int)span;

            if (draw_x < 0) {
                draw_w += draw_x;
                draw_x = 0;
            }
            if (draw_x + draw_w > screen_w) {
                draw_w = screen_w - draw_x;
            }
            if (draw_w > 0) {
                fb_fill_span(real_fb + y * screen_w + draw_x, draw_w, color);
            }
        }

        *col += span;
        if (*col >= movie->width) {
            *col = 0;
            (*row)++;
        }
        left -= span;
    }
}

static int decode_nyan_frame(const NyanMovie *movie, uint32_t payload_size,
                             int dst_x, int dst_y) {
    uint32_t pos = 0;
    uint32_t pixel = 0;
    uint32_t total = movie->width * movie->height;
    uint32_t row = 0;
    uint32_t col = 0;

    while (pos + 4 <= payload_size && pixel < total) {
        uint32_t count = le16(movie->frame_buf + pos);
        uint16_t color = le16(movie->frame_buf + pos + 2);
        pos += 4;

        if (count == 0) {
            continue;
        }
        if (pixel + count > total) {
            count = total - pixel;
        }

        draw_run(&row, &col, count, rgb565_to_xrgb(color), movie, dst_x, dst_y);
        pixel += count;
    }

    return pixel == total;
}

static int open_nyan_movie(NyanMovie *movie, const char *path) {
    uint8_t hdr[32];
    uint32_t format;
    uint64_t max_payload;

    memset(movie, 0, sizeof(*movie));
    movie->fd = -1;

    movie->fd = open(path, O_RDONLY, 0);
    if (movie->fd < 0) {
        return 0;
    }
    if (!read_exact_fd(movie->fd, hdr, sizeof(hdr)) ||
        memcmp(hdr, "NYANRLE1", 8) != 0) {
        close(movie->fd);
        movie->fd = -1;
        return 0;
    }

    movie->width = le32(hdr + 8);
    movie->height = le32(hdr + 12);
    movie->fps = le32(hdr + 16);
    movie->frame_count = le32(hdr + 20);
    format = le32(hdr + 24);
    if (movie->width == 0 || movie->height == 0 || movie->fps == 0 ||
        movie->frame_count == 0 || format != NYAN_FORMAT_RGB565_RLE) {
        close(movie->fd);
        movie->fd = -1;
        return 0;
    }

    max_payload = (uint64_t)movie->width * (uint64_t)movie->height * 4ULL;
    if (max_payload > 16ULL * 1024ULL * 1024ULL) {
        close(movie->fd);
        movie->fd = -1;
        return 0;
    }

    movie->frame_buf_size = (uint32_t)max_payload;
    movie->frame_buf = (uint8_t *)malloc(movie->frame_buf_size);
    if (!movie->frame_buf) {
        close(movie->fd);
        movie->fd = -1;
        return 0;
    }

    {
        off_t pos = lseek(movie->fd, 0, SEEK_CUR);
        if (pos < 0) {
            close(movie->fd);
            free(movie->frame_buf);
            movie->fd = -1;
            movie->frame_buf = NULL;
            return 0;
        }
        movie->data_start = (uint32_t)pos;
    }
    return 1;
}

static void close_nyan_movie(NyanMovie *movie) {
    if (movie->fd >= 0) {
        close(movie->fd);
    }
    free(movie->frame_buf);
    memset(movie, 0, sizeof(*movie));
    movie->fd = -1;
}

static int rewind_nyan_movie(NyanMovie *movie) {
    return lseek(movie->fd, (off_t)movie->data_start, SEEK_SET) >= 0;
}

static int skip_nyan_frame(NyanMovie *movie) {
    uint8_t len_buf[4];
    uint32_t payload_size;

    if (!read_exact_fd(movie->fd, len_buf, sizeof(len_buf))) {
        return 0;
    }
    payload_size = le32(len_buf);
    if (payload_size > movie->frame_buf_size) {
        return 0;
    }
    return skip_fd(movie->fd, payload_size);
}

static void flush_movie_rect(int dst_x, int dst_y, int w, int h) {
    Rect r;

    r.x = dst_x;
    r.y = dst_y;
    r.w = w;
    r.h = h;
    if (rect_clip(&r)) {
        flush_rect(r);
    }
}

static int play_nyan_loop(BootAudio *audio) {
    NyanMovie movie;
    uint32_t frame_ms;
    uint32_t next_tick;
    int dst_x;
    int dst_y;
    int stop = 0;

    if (!open_nyan_movie(&movie, BOOT_NYAN_PATH)) {
        return 0;
    }

    fb_fill_rect(0, 0, screen_w, screen_h, 0x00000000);
    NDL_FlushRect(0, 0, screen_w, screen_h);

    dst_x = (screen_w - (int)movie.width) / 2;
    dst_y = (screen_h - (int)movie.height) / 2;
    frame_ms = 1000u / movie.fps;
    if (frame_ms == 0) {
        frame_ms = 1;
    }
    next_tick = NDL_GetTicks();

    while (!stop) {
        for (uint32_t frame = 0; frame < movie.frame_count; frame++) {
            uint8_t len_buf[4];
            uint32_t payload_size;

            boot_audio_pump(audio);
            if (boot_stop_requested()) {
                stop = 1;
                break;
            }

            if (!read_exact_fd(movie.fd, len_buf, sizeof(len_buf))) {
                rewind_nyan_movie(&movie);
                break;
            }
            payload_size = le32(len_buf);
            if (payload_size > movie.frame_buf_size) {
                stop = 1;
                break;
            }

            if (!read_exact_fd(movie.fd, movie.frame_buf, (int)payload_size)) {
                stop = 1;
                break;
            }

            decode_nyan_frame(&movie, payload_size, dst_x, dst_y);
            flush_movie_rect(dst_x, dst_y, (int)movie.width, (int)movie.height);

            next_tick += frame_ms;
            for (uint32_t skipped = 0;
                 skipped < BOOT_MAX_SKIP_FRAMES &&
                 frame + 1 < movie.frame_count &&
                 (int32_t)(NDL_GetTicks() - next_tick) > (int32_t)frame_ms;
                 skipped++) {
                if (!skip_nyan_frame(&movie)) {
                    stop = 1;
                    break;
                }
                frame++;
                next_tick += frame_ms;
                boot_audio_pump(audio);
                if (boot_stop_requested()) {
                    stop = 1;
                    break;
                }
            }
            if (stop) {
                break;
            }
            if ((int32_t)(NDL_GetTicks() - next_tick) > 1000) {
                next_tick = NDL_GetTicks() + frame_ms;
            }
            if (wait_until(next_tick, audio)) {
                stop = 1;
                break;
            }
        }

        if (!rewind_nyan_movie(&movie)) {
            break;
        }
    }

    close_nyan_movie(&movie);
    return stop;
}

void run_boot_intro(void) {
    BootAudio audio;

    boot_audio_start(&audio);
    if (!play_terminal_intro(&audio)) {
        play_nyan_loop(&audio);
    }
    boot_audio_stop(&audio);
}
