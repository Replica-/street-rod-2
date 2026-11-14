/* ENHANCED (R1.2): the 3D race view's hooks in road_frame.
 *
 * In 3D mode road_frame still draws the original road and mirror (so every side effect of the 2D renderer stays),
 * then race3d_overlays replaces the overlay step: the view rectangle (without the mirror) is filled with colour 0
 * and the overlays drawn, the result saved, the view filled with colour 15 and the overlays drawn again. Pixels that
 * come out the same both times belong to an overlay (or the mirror); the rest is see-through, and the 3D view shows
 * there (ega_set_page_mask). The start-light slide's state is put back between the two passes. */
#include "game/race3d.h"

#include <string.h>

#include "game/race_int.h"
#include "platform/ega.h"
#include "r3d/r3d.h"

#define MODE DSS(DS_road_mode)

#define VIEW_Y0 18                  /* race view rows (race_render.md 1.3) */
#define VIEW_Y1 101
#define MIRROR_Y1 44                /* the mirror box: x 240..319 (byte 30 on), rows 18..44 */
#define MIRROR_B0 30

static u8 masks[3][320 * 200];
static u8 saved[4][(VIEW_Y1 - VIEW_Y0 + 1) * EGA_BYTES_PER_LINE];

bool race3d_on(void)
{
    if (!r3d_active() || r3d_classic()) return false;
    return MODE == 0 || MODE == 1 || MODE == 0x10;           /* R1.3; drag strips R1.6, Aqueduct R1.7 */
}

static u32 page_base(void) { return (u32)DSS(DS_back_page) * 0x2000u; }

static void fill_view(int colour)
{
    u32 base = page_base();
    for (int p = 0; p < 4; p++) {
        u8 v = (colour >> p & 1) ? 0xFF : 0x00, *pl = ega_plane(p);
        for (int y = VIEW_Y0; y <= VIEW_Y1; y++) {
            int bn = y <= MIRROR_Y1 ? MIRROR_B0 : EGA_BYTES_PER_LINE;
            memset(pl + ((base + (u32)y * EGA_BYTES_PER_LINE) & 0xFFFF), v, (size_t)bn);
        }
    }
}

static void save_view(void)
{
    u32 base = page_base();
    for (int p = 0; p < 4; p++)
        memcpy(saved[p], ega_plane(p) + ((base + VIEW_Y0 * EGA_BYTES_PER_LINE) & 0xFFFF), sizeof saved[p]);
}

static void build_mask(u8 *mask)
{
    u32 base = page_base();
    memset(mask, 0, 320 * 200);
    for (int y = VIEW_Y0; y <= VIEW_Y1; y++) {
        const u8 *now[4], *old[4];
        for (int p = 0; p < 4; p++) {
            now[p] = ega_plane(p) + ((base + (u32)y * EGA_BYTES_PER_LINE) & 0xFFFF);
            old[p] = saved[p] + (y - VIEW_Y0) * EGA_BYTES_PER_LINE;
        }
        for (int b = 0; b < EGA_BYTES_PER_LINE; b++) {
            u8 diff = (u8)((now[0][b] ^ old[0][b]) | (now[1][b] ^ old[1][b]) | (now[2][b] ^ old[2][b]) |
                           (now[3][b] ^ old[3][b]));
            if (!diff) continue;
            for (int bit = 0; bit < 8; bit++)
                if (diff & (0x80 >> bit)) mask[y * 320 + b * 8 + bit] = 1;
        }
    }
}

static void push_track(void)
{
    static R3dTrack t;
    for (int i = 0; i < R3D_TRACK_N; i++) {
        t.yaw[i] = TRKS(TRK_YAW, i);
        t.hgt[i] = TRKS(TRK_HGT, i);
        t.l[i] = TRKW(TRK_L, i);
        t.r[i] = TRKW(TRK_R, i);
        t.c[i] = TRKW(TRK_C, i);
        t.bl[i] = TRKS(TRK_BL, i);
        t.br[i] = TRKS(TRK_BR, i);
    }
    r3d_track_set(&t);
}

static void push_view(void)
{
    push_track();
    R3dView v;
    v.pos = (float)DSS(DS_seg) + (float)DSS(DS_substep) / 64.0f;
    v.cam_x = (float)DSS(DS_cam_x);
    v.cam_y = (float)DSS(DS_cam_y);
    v.heading = (float)DSS(DS_heading);
    v.height = (float)DSS(DS_cur_height);
    v.opp_pos = (float)DSS(DS_opp_seg) + (float)(DSS(DS_opp_seg_frac64) & 0x3F) / 64.0f;
    v.opp_lat = (float)DSS(DS_opp_lat);
    v.mode = MODE;
    r3d_view_push(&v);
}

void race3d_overlays(void (*draw_overlays)(void))
{
    s16 latch = DSS(DS_light_latch), active = DSS(DS_start_active), starter = DSS(DS_starter_seg);
    fill_view(0);
    draw_overlays();
    save_view();
    DSS(DS_light_latch) = latch;
    DSS(DS_start_active) = active;
    DSS(DS_starter_seg) = starter;
    fill_view(15);
    draw_overlays();
    int page = DSS(DS_back_page);
    if (page >= 0 && page < 3) {
        build_mask(masks[page]);
        ega_set_page_mask(page, masks[page]);
    }
    push_view();
}
