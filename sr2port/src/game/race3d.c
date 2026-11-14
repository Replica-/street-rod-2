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

/* ---------------------------------------------------------------- sprites (R1.4) */

#define PIC_MAX (320 * 200)
static u32 pic_px[PIC_MAX];

/* A RAM bitmap descriptor (video.h: planes one after the other, stride bytes per row, mask 1 = transparent)
 * as BGRA through the current palette registers. */
static bool decode_pic(FarPtr d, int *pw, int *ph)
{
    if (!d.seg && !d.off) return false;
    int w = desc_w(d), h = desc_h(d), st = desc_stride(d);
    if (w <= 0 || h <= 0 || w * h > PIC_MAX || st <= 0 || desc_type(d) == DESC_TYPE_EGA) return false;
    FarPtr pl = desc_planes(d), mk = desc_mask(d);
    u32 base = lin(pl.seg, pl.off), mbase = lin(mk.seg, mk.off), plane = (u32)st * (u32)h;
    bool has_mask = mk.seg || mk.off;
    if (base + 4 * plane > MEM_SIZE || (has_mask && mbase + plane > MEM_SIZE)) return false;
    u32 rgb[16];
    for (int i = 0; i < 16; i++) {
        u8 v = ega_get_palette_reg(i);
        rgb[i] = ega_palette[(v & 7) | (v >> 1 & 8)] | 0xFF000000u;
    }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            u32 o = (u32)y * st + (u32)(x >> 3);
            u8 bit = (u8)(0x80 >> (x & 7));
            int c = 0;
            for (int k = 0; k < 4; k++)
                if (mem[base + k * plane + o] & bit) c |= 1 << k;
            bool clear = has_mask ? (mem[mbase + o] & bit) != 0 : c == 0;
            pic_px[y * w + x] = clear ? 0 : rgb[c];
        }
    *pw = w;
    *ph = h;
    return true;
}

static FarPtr opp_pic(int s) { return ds_far((u16)(DS_opp_sprites + 4 * s)); }

/* The opponent (opponent_draw af98 has just run in the 2D frame and chosen view DS:A7D4 and picture DS:B198):
 * the largest picture of the same kind (side 0, rear quarters 1 / 3, rear 2 for 2 and 4..8), sized in the world
 * from the original's own size at its distance (screen px = 4 * world / z), smoothed. */
static void opp_sprite(R3dSprite *o)
{
    static float ew[13], eh[13];
    memset(o, 0, sizeof *o);
    if (!DSB(DS_racing) || MODE == 2) return;
    s16 s = DSS(DS_opp_sprite), d = DSS(DS_opp_last_d);
    if (s < 0 || s > 8) return;                            /* behind (mirror only) or not chosen */
    int kind = (s == 0 || s == 1 || s == 3) ? s : 2;
    int w, h;
    FarPtr cur = ds_far(DS_opp_pic);
    if (desc_w(cur) > 0) {
        int z = d / 8;
        if (s == 0) z += 0x10;
        if (z < -2 && s == 2) z += 8;
        z = (z < 0 ? -z : z) + 4;
        float sc = (float)z / 4.0f;                        /* world units per frame pixel at that depth */
        float ww = desc_w(cur) * sc, hh = desc_h(cur) * sc;
        if (ew[kind] <= 0) { ew[kind] = ww; eh[kind] = hh; }
        else { ew[kind] += (ww - ew[kind]) * 0.3f; eh[kind] += (hh - eh[kind]) * 0.3f; }
    }
    if (ew[kind] <= 0 || !decode_pic(opp_pic(kind), &w, &h)) return;
    r3d_sprite_upload(0, pic_px, w, h);
    o->vis = true;
    o->slot = 0;
    o->pos = (float)DSS(DS_opp_seg) + (float)DSS(DS_opp_seg_frac64) / 64.0f;
    o->lat = (float)DSS(DS_opp_lat);
    o->h = eh[kind];
    o->w = eh[kind] * (float)w / (float)h;                 /* the big picture's own aspect */
    o->along = s == 0;
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
    opp_sprite(&v.opp);
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
