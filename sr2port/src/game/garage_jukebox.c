/* ENHANCED: the garage juke box's menu (AdLib, no --classic): a click on the juke box (garage code -40) opens the
 * spare-part list (screen 7, "Change" renamed "Play it") with "Squelch it !" / "Catch some tunes" and the songs;
 * a song plays at once, then playlist A's rotation follows. The original only toggles the music (flow_loop.c). */
#include "game/garage.h"

#include <string.h>

#include "game/ds_shop.h"
#include "game/snd.h"
#include "game/ui.h"
#include "platform/platform.h"
#include "r3d/r3d.h"

/* in the stack area of the original (DS:BC50..C650, never used by the port's C code), as ds_flow.h / race_bob.c */
#define JUKE_LIST   0xC400u      /* s16[10]: the chosen song, then playlist A (DS:73A0), -1 */
#define JUKE_TEXT   0xC420u      /* the texts */

static const struct { int song; const char *name; } songs[] = {
    { 1, "Title theme" }, { 2, "Tune MSR2" }, { 3, "Tune MSR24" },
    { 4, "Ending tune" }, { 5, "Pause tune" }, { 6, "Unused MSR20" },
};
#define NSONGS (int)(sizeof songs / sizeof songs[0])

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

bool jukebox_menu_on(void)
{
    return !r3d_classic() && DSW(DS_snd_device) == 0 && DSB(DS_sound_gate) != 0;
}

void jukebox_menu(void)
{
    u16 at = JUKE_TEXT, texts[1 + NSONGS];
    int song_of[1 + NSONGS], n = 0;
    texts[n] = DSW(DS_music_on) ? DS_lbl_squelch : DS_lbl_tunes;
    song_of[n++] = 0;
    for (int i = 0; i < NSONGS; i++)
        if (mus_song_ready(songs[i].song)) {
            texts[n] = ds_put(&at, songs[i].name);
            song_of[n++] = songs[i].song;
        }
    u16 play = ds_put(&at, "Play it");
    memcpy(mp(DGROUP, DS_list_entries), texts, sizeof(u16) * (size_t)n);
    u16 e = screen_element(7, -3);
    DSW((u16)(e + 2)) = play;
    s16 r = list_box(7, 0, (const u16 *)mp(DGROUP, DS_list_entries), NULL, (s16)n);
    DSW((u16)(e + 2)) = DS_TXT_CHANGE;
    if (r != -3) return;                            /* Forget it */
    int song = song_of[list_selected()];
    if (song == 0) {                                /* the original toggle */
        if (DSW(DS_music_on) == 0) { DSW(DS_music_on) = 1; music_resume(); }
        else { music_stop(); DSW(DS_music_on) = 0; }
        return;
    }
    DSS(JUKE_LIST) = (s16)song;
    for (int i = 0; i < 9; i++) {
        s16 v = DSS((u16)(DS_song_list_a + 2 * i));
        DSS((u16)(JUKE_LIST + 2 + 2 * i)) = v;
        if (v < 0) break;
    }
    mus_list_play(JUKE_LIST);
    DSW(DS_music_on) = 1;
    music_resume();                                 /* the juke box label: "Squelch it !" */
}
