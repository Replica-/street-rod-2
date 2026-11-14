/* Platform: start-up - main 0001:07b0 up to the game loop, the text-mode video and sound option menus
 * (0001:00c0, 0001:01ea), video detection 0001:037c, driver selection 0001:046e, the video mode set
 * 0001:0628, the CPU speed class - platform.md "Start-up", game_flow.md main, sound.md 0001:01ea. */
#include "platform/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "modules.h"
#include "platform/textmode.h"
#include "platform/vga.h"
#include "platform/video.h"

/* PORT: user decision 2026-11-14: the "Video Options" menu 0001:00c0 and "Sound Options" menu 0001:01ea are
 * not shown; without a switch the game starts in VGA with AdLib (the switches still choose). */

/* =========================================================================== video detection */

/* 0001:037c video_detect_menu(forced): adapter detection, the recommendation and the menu (shown only when
 * no mode was forced by a switch); returns the mode code.
 * PORT: the detection (INT 10h 1Ah/12h, Hercules, Tandy ROM signature) is replaced by the state the original
 * leaves on a VGA with 256 KB: the recommendation is 3 ("VGA"). Choices 1, 4, 5 (CGA, Hercules, Tandy) are not
 * ported; choice 2 (EGA) is run as VGA (the same driver, the split screen model is the VGA one). */
static s16 video_detect_menu(s16 forced)
{
    DSW(DS_ega_present) = 1;
    DSB(DS_bios_mode_saved) = 0x03;                               /* BIOS mode at start (80x25 colour text) */
    DSB(DS_bios_equip_saved) = rd8(0x0040, 0x0010);               /* equipment byte */
    DSB(DS_bios_ega_info) = 0;                                    /* EGA info: colour */
    DSB(DS_bios_ega_mem) = 3;                                     /* 256 KB */
    DSB(DS_bios_ega_switch) = 9;                                  /* switch setting */
    s16 si = 3;                                                   /* vga_detect() != 0 */
    if (forced != 0) si = forced;                                /* PORT: no video menu, VGA by default */
    if (si == 1 && DSW(DS_ega_present) != 0) return 6;
    if (si == 3) {
        DSW(DS_g_vga) = 1;
        return 2;
    }
    return si;
}

/* 0001:046e driver_select - only the EGA/VGA cases exist in the port. */
void driver_select(s16 mode)
{
    switch (mode) {
    case 3:
        DSW(DS_g_vga) = 1;
        /* fall through */
    case 2:
        DSS(DS_driver_id) = -2;
        wr8(0x0000, 0x0410, (u8)((rd8(0x0000, 0x0410) & 0xCF) | 0x20));   /* equipment: 80x25 colour */
        ega_install_vectors();                    /* 2f32:111c: DS:7B38 -> DS:A0AE (20 pointers) */
        return;
    default:
        /* PORT: CGA (1, 6), Hercules (4) and Tandy (5) drivers are not part of the port. */
        host_fatal("driver_select: video mode %d is not supported", mode);
    }
}

/* 0001:0628 video_mode_init */
void video_mode_init(s16 mode)
{
    switch (mode) {
    case 2:
    case 3:
        vga_mode_0d();                            /* bios_set_mode(0Dh): planes cleared; cga_set_palette(1) is a no-op on VGA */
        break;
    default:
        host_fatal("video_mode_init: video mode %d is not supported", mode);   /* PORT: see driver_select */
    }
    DSW(DS_gfx_mode_set) = 1;
}

/* 0001:079a slow_flag_set */
void slow_flag_set(void)
{
    DSW(DS_slow_machine) = DSS(DS_cpu_speed) < 4 ? 1 : 0;
}

/* 0001:07ae empty_stub: a bare retf. */
void empty_stub(void)
{
}

/* 34b0:2226 stricmp(a, b) == 0 (ASCII case folding, as the MS C runtime) */
static bool stricmp_eq(const char *a, const char *b)
{
    for (;; a++, b++) {
        u8 ca = (u8)*a, cb = (u8)*b;
        if (ca >= 'A' && ca <= 'Z') ca = (u8)(ca + 0x20);
        if (cb >= 'A' && cb <= 'Z') cb = (u8)(cb + 0x20);
        if (ca != cb) return false;
        if (ca == 0) return true;
    }
}

/* 0001:07b0 main, initialisation part: everything up to the call of game_loop. */
void platform_main_init(int argc, char **argv)
{
    dos_init();                                   /* PORT: the DOS state at program start */
    video_register_codeptrs();                    /* PORT: far code pointers stored in game data */
    modules_init();                               /* PORT: the hooks into the other modules */

    empty_stub();
    DSB(DS_ui_level) = 0;
    /* PORT: 0001:070c cpu_speed_calibrate (loop count during one BIOS tick) -> a fixed fast class. */
    DSW(DS_cpu_speed) = SR_CPU_SPEED;
    s16 disks = 2, forced = 0, forced_snd = -1;   /* PORT: launcher: forced_snd 0 = AdLib, 1 = PC speaker */
    DSW(DS_sw_demo) = 0;
    DSW(DS_auto_drive) = 0;
    DSW(DS_mouse_enabled) = 1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (memcmp(a, ds_str(DS_SW_ONE), 2) == 0) disks = 1;                 /* "1", exact */
        else if (stricmp_eq(a, ds_str(DS_SW_NOMOUSE))) DSW(DS_mouse_enabled) = 0;
        else if (stricmp_eq(a, ds_str(DS_SW_DEMO))) DSW(DS_sw_demo) = 1;
        else if (stricmp_eq(a, ds_str(DS_SW_NOUEMEM))) DSW(DS_no_ega_park) = 1;
        else if (stricmp_eq(a, ds_str(DS_SW_E))) forced = 2;                 /* EGA */
        else if (stricmp_eq(a, ds_str(DS_SW_C))) forced = 1;                 /* CGA */
        else if (stricmp_eq(a, ds_str(DS_SW_H))) forced = 4;                 /* Hercules */
        else if (stricmp_eq(a, ds_str(DS_SW_V))) forced = 3;                 /* VGA */
        else if (stricmp_eq(a, ds_str(DS_SW_T))) forced = 5;                 /* Tandy */
        else if (stricmp_eq(a, "adlib")) forced_snd = 0;                    /* PORT: launcher: skips the sound options menu */
        else if (stricmp_eq(a, "speaker")) forced_snd = 1;                  /* PORT: launcher: PC speaker ("Normal") */
    }
    s16 mode = video_detect_menu(forced);
    s16 snd = forced_snd >= 0 ? forced_snd : 0;                  /* PORT: no sound menu, AdLib by default */
    if (getenv("SR_TRACE")) fprintf(stderr, "trace: startup video mode %d (%s) sound %d (%s)\n", mode, forced ? "switch" : "default", snd, forced_snd >= 0 ? "switch" : "default");   /* PORT: developer aid */
    /* DS:0050 stays 666 until the sound init below (the timer ISR and platform_install test it) */
    mem_pools_init();
    driver_select(mode);
    video_mode_init(mode);
    gfx_init(far_make(SEG(0x371C), 0x0000), far_make(SEG(0x371C), 0x0012));
    arena_init();
    platform_install();
    modules.snd_init(snd);                        /* speaker_sound_init 2c57:05c8 / adlib_sound_init 2c57:13d8 (S5.4) */
    if (DSS(DS_driver_id) == -2 || DSS(DS_driver_id) == -6) ega_set_palette(DS_pal_live);
    lib_open_all(disks, mode);
    slow_flag_set();
    rnd(-1);                                      /* seeds the generator from the clock */
    lib_read_dir(1);
    cursor_init();
    gfx_screen_mode(0);
    /* (driver -3: a 320x100 back bitmap in DS:9AD4; not on the VGA path, and SR2 has no picture work buffer) */
    if (pic_load_list(DS_BIG_PICS, 0) == 3) {     /* enough memory for the big LIB1 pictures {3, 1, 4} */
        DSW(DS_libs_preloaded) = 1;
        lib_read_dir(2);
        if (DSW(DS_snd_device) == 0) modules.mus_songs_load();
        hot_data_load();
        hall_load();
        if (DSS(DS_driver_id) == -2) pic_park_list(DS_RESIDENT_PICS);
        pic_load_list(DS_RESIDENT_PICS, 1);
    } else {
        DSW(DS_libs_preloaded) = 0;
    }
    /* main continues with wait_click_or_key(1), game_loop 0001:4dd6 and platform_exit (game_main) */
}
