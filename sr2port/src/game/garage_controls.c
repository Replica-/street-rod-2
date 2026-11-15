/* ENHANCED: the garage clock's "Controls" (the "Save, Restart, or Quit." box, garage code 4, gets a Controls button):
 * the controller on / off and its gear up / down buttons, and a key of your own for each driving action, in the
 * spare-part list (screen 7, as the juke box menu); kept in controls.cfg in the game folder (host.c). */
#include "game/garage.h"

#include <stdio.h>
#include <string.h>

#include "game/ds_shop.h"
#include "game/ui.h"
#include "host.h"
#include "platform/platform.h"
#include "platform/video.h"

/* in the stack area of the original (DS:BC50..C650, never used by the port's C code), as ds_flow.h / race_bob.c */
#define CTL_MENU    0xBC60u      /* the "Save, Restart, or Quit." box with a Controls button: 7 elements */
#define CTL_TEXT    0xBD00u      /* the texts (to DS:BFFF) */
#define CODE_CONTROLS (-48)

static u16 ds_put(u16 *at, const char *s)
{
    u16 o = *at;
    strcpy(ds_str(o), s);
    *at = (u16)(o + strlen(s) + 1);
    return o;
}

static u16 screen_element(s16 scr, s16 code)
{
    u16 e = DSW((u16)(DS_SCREENS + 2 * scr));
    while (DSS((u16)(e + 6)) != code) e = (u16)(e + 0x12);
    return e;
}

/* SDL's gamepad button names, as a player knows them */
static const char *pad_name(const char *n)
{
    static const struct { const char *sdl, *nice; } t[] = {
        { "a", "A" }, { "b", "B" }, { "x", "X" }, { "y", "Y" }, { "leftshoulder", "LB" }, { "rightshoulder", "RB" },
        { "leftstick", "Left stick" }, { "rightstick", "Right stick" }, { "back", "Back" }, { "start", "Start" },
        { "guide", "Guide" }, { "dpup", "D-pad up" }, { "dpdown", "D-pad down" }, { "dpleft", "D-pad left" },
        { "dpright", "D-pad right" },
    };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) if (!strcmp(n, t[i].sdl)) return t[i].nice;
    return n;
}

enum { ROW_PAD, ROW_PAD_UP, ROW_PAD_DOWN, ROW_KEY0, ROW_RESET = ROW_KEY0 + HC_KEYS, ROWS };

/* waits for a key (pad 0) or a pad button (pad 1); false on Esc */
static bool capture(int pad, const char *what, int *code)
{
    char msg[64];
    snprintf(msg, sizeof msg, pad ? "Press a controller button for %s (Esc: cancel)" : "Press a key for %s (Esc: cancel)", what);
    status_print(-1, msg, 0);
    host_capture_begin(pad);
    int st;
    while ((st = host_capture_poll(code)) == 0) host_pump();
    status_print(-1, "", 0);
    return st > 0;
}

void controls_menu(void)
{
    s16 sel = 0;
    for (;;) {
        u16 at = CTL_TEXT, texts[ROWS];
        char line[48];
        snprintf(line, sizeof line, "Controller: %s", host_pad_enabled() ? "On" : "Off");
        texts[ROW_PAD] = ds_put(&at, line);
        snprintf(line, sizeof line, "Pad gear up: %s", pad_name(host_pad_button(1)));
        texts[ROW_PAD_UP] = ds_put(&at, line);
        snprintf(line, sizeof line, "Pad gear down: %s", pad_name(host_pad_button(0)));
        texts[ROW_PAD_DOWN] = ds_put(&at, line);
        for (int a = 0; a < HC_KEYS; a++) {
            snprintf(line, sizeof line, "%s: %s", host_control_name(a), host_control_key(a));
            texts[ROW_KEY0 + a] = ds_put(&at, line);
        }
        texts[ROW_RESET] = ds_put(&at, "Reset to defaults");
        u16 change = ds_put(&at, "Change"), done = ds_put(&at, "Done"), heading = ds_put(&at, "Controls");
        memcpy(mp(DGROUP, DS_list_entries), texts, sizeof texts);
        u16 e3 = screen_element(7, -3), e4 = screen_element(7, -4), title = 0, title_was = 0, e4_was = DSW((u16)(e4 + 2));
        for (u16 q = DSW((u16)(DS_SCREENS + 2 * 7)), k = 0; k < 40 && DSW(q) != 0xFFFF; k++, q = (u16)(q + 0x12))
            if (DSW((u16)(q + 2)) && !strcmp(ds_str(DSW((u16)(q + 2))), "Spare parts")) { title = q; break; }
        DSW((u16)(e3 + 2)) = change;
        DSW((u16)(e4 + 2)) = done;
        if (title) { title_was = DSW((u16)(title + 2)); DSW((u16)(title + 2)) = heading; }
        s16 r = list_box(7, sel, (const u16 *)mp(DGROUP, DS_list_entries), NULL, ROWS);
        DSW((u16)(e3 + 2)) = DS_TXT_CHANGE;
        DSW((u16)(e4 + 2)) = e4_was;
        if (title) DSW((u16)(title + 2)) = title_was;
        if (r != -3) return;                        /* Done */
        sel = list_selected();
        int code;
        if (sel == ROW_PAD) host_pad_enable(!host_pad_enabled());
        else if (sel == ROW_PAD_UP || sel == ROW_PAD_DOWN) {
            if (capture(1, sel == ROW_PAD_UP ? "gear up" : "gear down", &code)) host_pad_set_button(sel == ROW_PAD_UP, code);
        } else if (sel >= ROW_KEY0 && sel < ROW_KEY0 + HC_KEYS) {
            if (capture(0, host_control_name(sel - ROW_KEY0), &code)) host_control_set_key(sel - ROW_KEY0, code);
        } else if (sel == ROW_RESET) host_controls_reset();
        host_controls_save();
    }
}

/* the "Save, Restart, or Quit." box (screen 1) with a Controls button: Quit and Forget it move up / down in the right
 * column, Controls between them (key C); DS_SCREENS[1] points at this copy while the box is up */
u16 controls_quit_screen(void)
{
    u16 src = DSW((u16)(DS_SCREENS + 2)), dst = CTL_MENU, n = 0, k = 0;
    for (u16 q = src; k < 6 && DSW(q) != 0xFFFF; q = (u16)(q + 0x12), k++) {
        s16 code = DSS((u16)(q + 6));
        if (code == -22) {                          /* Forget it: Controls before it */
            u16 c = (u16)(dst + 0x12 * n++);
            memcpy(mp(DGROUP, c), mp(DGROUP, q), 0x12);
            DSW(c) = 0x11;                          /* not the last element */
            DSW((u16)(c + 2)) = (u16)(CTL_MENU + 0x12 * 7);
            DSS((u16)(c + 6)) = CODE_CONTROLS;
            DSS((u16)(c + 10)) = 114; DSS((u16)(c + 14)) = 126;
            DSW((u16)(c + 16)) = 0xFF2E;            /* C */
        }
        u16 d = (u16)(dst + 0x12 * n++);
        memcpy(mp(DGROUP, d), mp(DGROUP, q), 0x12);
        if (code == -20) { DSS((u16)(d + 10)) = 96; DSS((u16)(d + 14)) = 108; }      /* Quit */
        if (code == -22) { DSS((u16)(d + 10)) = 132; DSS((u16)(d + 14)) = 144; }     /* Forget it (the last) */
        if (DSW(d) & 2) break;
    }
    strcpy(ds_str((u16)(CTL_MENU + 0x12 * 7)), "Controls");
    return dst;
}

bool controls_code(s16 r) { return r == CODE_CONTROLS; }
