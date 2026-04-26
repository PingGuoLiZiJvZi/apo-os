#include "desktop.h"

int poll_events(int *out_key, int *out_key_type,
                int *out_mouse_abs_code, int *out_mouse_abs_val) {
    *out_key = 0;
    *out_key_type = 0;
    *out_mouse_abs_code = -1;
    *out_mouse_abs_val = 0;

    if (evtdev < 0) return 0;

    char buf[64];
    int n = read(evtdev, buf, sizeof(buf) - 1);
    if (n <= 0) return 0;
    buf[n] = '\0';

    int code = 0, val = 0;
    if (sscanf(buf, "kd %d", &code) == 1) {
        *out_key = code;
        *out_key_type = 1; /* key down */
        return 1;
    }
    if (sscanf(buf, "ku %d", &code) == 1) {
        *out_key = code;
        *out_key_type = 2; /* key up */
        return 1;
    }
    if (sscanf(buf, "ma %d %d", &code, &val) == 2) {
        *out_mouse_abs_code = code;
        *out_mouse_abs_val = val;
        return 1;
    }
    return 0;
}
