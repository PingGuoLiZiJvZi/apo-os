#include <BMP.h>

#include "desktop.h"

#define DESKTOP_DIR "/desktop"
#define DESK_SUFFIX ".desk"
#define DESK_NAME_MAX 28
#define DESK_FILE_MAX 1024

typedef struct {
    uint32_t inum;
    char name[DESK_NAME_MAX];
} DiskDirEntry;

static void copy_str(char *dst, int dst_size, const char *src) {
    int i = 0;
    if (!dst || dst_size <= 0) return;
    if (!src) src = "";
    for (; i < dst_size - 1 && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static int is_space_ch(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static char *trim(char *s) {
    char *end;
    while (*s && is_space_ch(*s)) s++;
    end = s + strlen(s);
    while (end > s && is_space_ch(end[-1])) {
        end--;
    }
    *end = '\0';
    return s;
}

static int ends_with(const char *name, const char *suffix) {
    int nl = (int)strlen(name);
    int sl = (int)strlen(suffix);
    if (nl < sl) return 0;
    return strcmp(name + nl - sl, suffix) == 0;
}

static uint32_t default_app_color(int idx) {
    static const uint32_t colors[] = {
        0x00e06060, 0x0060c060, 0x006080e0, 0x00c0c040,
        0x00c060c0, 0x0000a0c0, 0x00d09040, 0x0080c080
    };
    return colors[idx % (int)(sizeof(colors) / sizeof(colors[0]))];
}

static int parse_bool(const char *s) {
    return strcmp(s, "1") == 0 ||
           strcmp(s, "true") == 0 ||
           strcmp(s, "TRUE") == 0 ||
           strcmp(s, "yes") == 0 ||
           strcmp(s, "YES") == 0;
}

static int arg_is_null(const char *s) {
    return s[0] == '\0' ||
           strcmp(s, "NULL") == 0 ||
           strcmp(s, "null") == 0 ||
           strcmp(s, "none") == 0 ||
           strcmp(s, "NONE") == 0;
}

static void layout_desktop_apps(void) {
    int desktop_h = screen_h - TASKBAR_H;
    int rows = 1;

    if (desktop_h > ICON_GAP) {
        rows = (desktop_h - ICON_GAP) / DESKTOP_ICON_CELL_H;
        if (rows < 1) rows = 1;
    }

    for (int i = 0; i < num_apps; i++) {
        int col = i / rows;
        int row = i % rows;
        int cell_x = ICON_GAP + col * DESKTOP_ICON_CELL_W;
        int cell_y = ICON_GAP + row * DESKTOP_ICON_CELL_H;

        apps[i].cell_rect.x = cell_x;
        apps[i].cell_rect.y = cell_y;
        apps[i].cell_rect.w = DESKTOP_ICON_CELL_W;
        apps[i].cell_rect.h = DESKTOP_ICON_CELL_H;
        apps[i].icon_rect.x = cell_x + (DESKTOP_ICON_CELL_W - DESKTOP_ICON_SIZE) / 2;
        apps[i].icon_rect.y = cell_y;
        apps[i].icon_rect.w = DESKTOP_ICON_SIZE;
        apps[i].icon_rect.h = DESKTOP_ICON_SIZE;
    }
}

static int parse_desktop_file(const char *filename, AppEntry *app) {
    char path[APP_PATH_MAX];
    char buf[DESK_FILE_MAX + 1];
    int fd;
    int n;
    int type_exec = 0;
    int has_name = 0;

    memset(app, 0, sizeof(*app));
    app->color = default_app_color(num_apps);
    app->pref_w = 320;
    app->pref_h = 240;
    app->centered = 0;

    sprintf(path, "%s/%s", DESKTOP_DIR, filename);
    fd = open(path, O_RDONLY, 0);
    if (fd < 0) return 0;
    n = read(fd, buf, DESK_FILE_MAX);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';

    char *line = buf;
    while (*line) {
        char *next = line;
        char *eq;
        while (*next && *next != '\n') next++;
        if (*next == '\n') {
            *next = '\0';
            next++;
        }

        line = trim(line);
        if (*line == '\0' || *line == '#') {
            line = next;
            continue;
        }

        eq = strchr(line, '=');
        if (!eq) {
            line = next;
            continue;
        }
        *eq = '\0';
        char *key = trim(line);
        char *val = trim(eq + 1);

        if (strcmp(key, "type") == 0) {
            type_exec = strcmp(val, "exec") == 0;
        } else if (strcmp(key, "name") == 0) {
            copy_str(app->title, sizeof(app->title), val);
            has_name = app->title[0] != '\0';
        } else if (strcmp(key, "icon") == 0) {
            copy_str(app->icon_path, sizeof(app->icon_path), val);
        } else if (strcmp(key, "path") == 0) {
            copy_str(app->path, sizeof(app->path), val);
        } else if (strcmp(key, "arg") == 0) {
            if (!arg_is_null(val)) {
                copy_str(app->arg1, sizeof(app->arg1), val);
                app->has_arg = 1;
            }
        } else if (strcmp(key, "w") == 0 || strcmp(key, "width") == 0) {
            app->pref_w = atoi(val);
        } else if (strcmp(key, "l") == 0 || strcmp(key, "h") == 0 ||
                   strcmp(key, "height") == 0) {
            app->pref_h = atoi(val);
        } else if (strcmp(key, "centered") == 0) {
            app->centered = parse_bool(val);
        }

        line = next;
    }

    if (!type_exec || !has_name || app->path[0] == '\0') {
        return 0;
    }
    if (app->pref_w <= 0) app->pref_w = 320;
    if (app->pref_h <= 0) app->pref_h = 240;

    if (app->icon_path[0]) {
        app->icon_pixels = (uint32_t *)BMP_Load(app->icon_path, &app->icon_w, &app->icon_h);
        if (!app->icon_pixels) {
            app->icon_w = 0;
            app->icon_h = 0;
        }
    }
    return 1;
}

static void sort_names(char names[MAX_APPS][DESK_NAME_MAX + 1], int count) {
    for (int i = 1; i < count; i++) {
        char tmp[DESK_NAME_MAX + 1];
        int j = i - 1;
        copy_str(tmp, sizeof(tmp), names[i]);
        while (j >= 0 && strcmp(names[j], tmp) > 0) {
            copy_str(names[j + 1], DESK_NAME_MAX + 1, names[j]);
            j--;
        }
        copy_str(names[j + 1], DESK_NAME_MAX + 1, tmp);
    }
}

void load_desktop_apps(void) {
    DiskDirEntry de;
    char names[MAX_APPS][DESK_NAME_MAX + 1];
    int name_count = 0;
    int fd;

    num_apps = 0;
    memset(apps, 0, sizeof(apps));

    fd = open(DESKTOP_DIR, O_RDONLY, 0);
    if (fd < 0) {
        printf("[desktop] no %s directory\n", DESKTOP_DIR);
        return;
    }

    while (name_count < MAX_APPS &&
           read(fd, &de, sizeof(de)) == (int)sizeof(de)) {
        if (de.inum == 0) continue;
        char name[DESK_NAME_MAX + 1];
        memcpy(name, de.name, DESK_NAME_MAX);
        name[DESK_NAME_MAX] = '\0';
        if (!ends_with(name, DESK_SUFFIX)) continue;
        copy_str(names[name_count++], DESK_NAME_MAX + 1, name);
    }
    close(fd);

    sort_names(names, name_count);

    for (int i = 0; i < name_count && num_apps < MAX_APPS; i++) {
        AppEntry app;
        if (!parse_desktop_file(names[i], &app)) {
            continue;
        }
        apps[num_apps++] = app;
    }

    layout_desktop_apps();
    printf("[desktop] loaded %d desktop apps\n", num_apps);
}

int hit_desktop_app(int mx, int my) {
    if (my >= screen_h - TASKBAR_H) return -1;

    for (int i = 0; i < num_apps; i++) {
        Rect r = apps[i].cell_rect;
        if (mx >= r.x && mx < r.x + r.w &&
            my >= r.y && my < r.y + r.h) {
            return i;
        }
    }
    return -1;
}
