/* PC picture id -> Amiga picture (lib 0 = disk2/lib.p, 1 = disk1/libstart.p, index). Candidates from
 * tools/amiga_map.py (same size, same shape), checked by eye 2026-11-14 (work/amap1.png, amap2.png): car body pieces
 * and opponent cars left out (painted per car through the palette registers), wrong pairs dropped, the portraits in
 * order (Amiga = PC id - 1002). Race interior: the three dashboards (with their steering wheels), the mirror frame and
 * the windscreen backdrop (table 1; the dashboards' top band is the car's paint); no Amiga A-pillar or gear box. pal = the STREET_ROD palette table, picked by eye (work/amap_pal7.png,
 * race_tables.png, start_tables.png): 7 for the town / garage / menus.
 * Numbers only; the pictures are the user's own files. */
static const struct { short pc_id; unsigned char lib; short index; unsigned char pal; } amiga_map[] = {
    { 1, 1, 39, 5 },                                           /* title (the Amiga's: the Mustang at sunset) */
    { 3, 1, 30, 6 },                                           /* California Dreams logo (cut, tools/amiga_export.py) */
    { 4, 1, 31, 2 },                                           /* LIB1 #3 wanted poster */
    { 1001, 0, 115, 7 }, { 1002, 0, 132, 7 }, { 1003, 0, 116, 7 },   /* police book, driving licence, menu bar */
    { 1004, 0, 117, 7 }, { 1005, 0, 118, 7 }, { 1006, 0, 119, 7 },   /* load / save / forget it */
    { 1007, 0, 32, 7 }, { 1013, 0, 102, 7 }, { 1018, 0, 144, 7 },    /* newspaper headers, classifieds, garage */
    { 1055, 0, 106, 7 },                                       /* engine screen */
    { 1072, 0, 142, 7 }, { 1073, 0, 120, 7 },                     /* gas station, Burger's Bungalow (redrawn: below
                                                                 * the matcher's threshold, checked by eye) */
    { 1209, 0, 114, 7 },                                       /* King Street Rodders wall */
    { 1057, 0, 0, 7 }, { 1058, 0, 1, 7 }, { 1059, 0, 2, 7 },         /* speech bubbles */
    { 1083, 1, 32, 9 },                                        /* ending text panel */
    { 1110, 0, 122, 7 }, { 1111, 0, 123, 7 },
    { 1139, 0, 125, 7 }, { 1140, 0, 126, 7 }, { 1141, 0, 127, 7 }, { 1142, 0, 128, 7 },   /* tool icons */
    { 1144, 0, 134, 7 }, { 1146, 0, 133, 7 },                     /* calendars */
    { 1161, 0, 159, 7 }, { 1162, 0, 160, 7 }, { 1163, 0, 161, 7 }, { 1164, 0, 162, 7 }, { 1165, 0, 163, 7 },   /* portraits */
    { 1166, 0, 164, 7 }, { 1167, 0, 165, 7 }, { 1168, 0, 166, 7 }, { 1169, 0, 167, 7 }, { 1170, 0, 168, 7 },
    { 1171, 0, 169, 7 }, { 1172, 0, 170, 7 }, { 1173, 0, 171, 7 }, { 1174, 0, 172, 7 }, { 1175, 0, 173, 7 },
    { 1176, 0, 174, 7 }, { 1177, 0, 175, 7 }, { 1178, 0, 176, 7 }, { 1179, 0, 177, 7 }, { 1180, 0, 178, 7 },
    { 1198, 0, 264, 7 },
    { 2011, 0, 70, 1 }, { 2012, 0, 78, 1 }, { 2019, 0, 77, 1 },   /* the three dashboards, steering wheels included */
    { 2072, 0, 83, 1 }, { 2073, 0, 84, 1 }, { 2074, 0, 85, 1 },   /* their wheels straight, turned left, turned right */
    { 2021, 0, 87, 1 }, { 2023, 0, 88, 1 }, { 2024, 0, 89, 1 },
    { 2077, 0, 91, 1 }, { 2076, 0, 92, 1 }, { 2075, 0, 93, 1 },
    { 2005, 0, 67, 1 }, { 2002, 0, 39, 1 },                   /* rear-view mirror frame, windscreen backdrop */
};
