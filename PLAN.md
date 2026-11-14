# Street Rod 2 Enhanced — plan

## Status
Set up 2026-11-14 from the faithful port (`../street-rod-2` at 75326ff). Next: **R1.7**.

## User to-do
| What | Why |
|---|---|
| Drive a cruise and a Mulholland race in the 3D view: smooth? road where you expect it, curves and hills feel right? (`--classic` = old view) | R1.2 / R1.3 |
| Play a little in the new OpenGL window: title, garage, a race; resize the window, Alt+Enter, mouse clicks land where the pointer is | R1.1 replaced SDL's renderer; it should look and click exactly as before |

## Phase R1 — 3D race view
The race view (front strip (0,18)-(319,101), mirror (240,22)-(319,42), `race_render.md` §1.3) is drawn by a GPU
renderer instead of the original road routines; the dashboard, cockpit overlay and status line stay the original
2D frame, drawn on top. Game state is read after each physics step and interpolated per displayed frame.

| Id | Task | Size | Status |
|---|---|---|---|
| R1.1 | Renderer base: SDL3 GPU (or OpenGL 3.3) context in `host.c`; the 320x200 frame becomes a texture drawn over a 3D viewport; `classic` switch keeps the 2D path; frame pacing at display refresh | M | done 2026-11-14 (user check pending) |
| R1.2 | State snapshot + interpolation: capture player seg/substep, lateral position, heading, speed, wheel, opponent / police positions at each physics step (race ISR, every 12th tick); blend the last two by elapsed time each frame | M | done 2026-11-14 |
| R1.3 | 3D road (modes 0, 1, 0x10): mesh from track arrays YAW/HGT/L/R/C (+BL/BR banks for Mulholland), road surface, lines, shoulders, sky and horizon; camera at the interpolated player position | L | done 2026-11-14 |
| R1.4 | Sprites: opponent, police, roadside scenery (SR2_SHP shapes and LIB pictures) as billboards placed from the same data the 2D renderer uses | L | done 2026-11-14 |
| R1.5 | Rear-view mirror as a second camera into the mirror rectangle | S | done 2026-11-14 |
| R1.6 | Drag strips (modes 2, 4): barriers, start tree, starter figure | M | done 2026-11-14 |
| R1.7 | Aqueduct (mode 8): concrete channel mesh from the AQ flags, roll, rotating opponent sprites | L | |
| R1.8 | Crashes, cliff fall, breakdowns, Bob's Drive-In / jail scenes: check each against the 2D path | M | |

## Phase R2 — effects
| Id | Task | Size | Status |
|---|---|---|---|
| R2.1 | Rain: particles, streaks on the windscreen, wet road (darker, reflections) | M | |
| R2.2 | Spray and tyre smoke behind cars | S | |
| R2.3 | Fog / dusk / night lighting with headlights | M | |

## Later (from the faithful repo's Enhanced backlog)
| Id | Idea | Size |
|---|---|---|
| E3 | Launcher: graphics options (3D / classic, effects) | M |
| E4 | Gamepad and key bindings | S |
| E5 | Widescreen / window scaling for the 3D view | M |
| E6 | Music option: no ~136 s silence between playlist songs | S |
| E7 | Amiga graphics option (`tools/amilib.py`; palettes from the Amiga exe) | L |
| — | AI-redrawn garage art (Fooocus experiment 2026-11-14, faithful repo `work/fooocus`) | M |

Notes:
- R1.1: `src/r3d/r3d.c` (OpenGL 3.3 core, GL loaded via SDL_GL_GetProcAddress, frame = BGRA texture, 4:3 letterbox, vsync); `host.c` presents through it and maps the mouse with `r3d_letterbox`; no context (SDL_VIDEO_DRIVER=dummy) = not shown, game runs (smoke tests unchanged); `--classic` parsed in main.c (no effect until R1.3).
- R1.2: the view is captured per original frame (road_frame, ~9 Hz: every way the game moves the camera is covered) and blended one frame behind (`r3d.c` view_now). 3D mode keeps the original 2D drawing (side effects), then `game/race3d.c` repaints the view twice (colour 0 / 15) under the overlays: unchanged pixels = overlay or mirror, the rest see-through (`ega_set_page_mask`, alpha in compose). Test scene in `r3d/scene.c`. Checked headless (`SR_HIDDEN=1`, snapshots read back from the off-screen buffer; `SR_R3D_TRACE=1` prints the blended view): 110 fps unpaced, speed per frame within ±10 %, no stalls.
- R1.3: `r3d/scene.c` builds 160 segments ahead per frame (original: 15). Scale from the original projection: 1 depth unit = 64 lateral units (focal 256 frame px, ~64° view), segment 512, YAW unit 1/256 rad, HGT unit 30, eye 0x7D0 - cam_y above the road; anisotropic projection on the 320x84 view strip, so near geometry lines up with the 2D view. Track data (yaw -756..280, hgt -10..140, banks +-4000) pushed by race3d.c each original frame (copied when changed). Road / shoulder / grass bands, centre dash (C & 8), Mulholland banks (hill / drop-off), distance fog. Checked headless on cruise and Mulholland. Open: distant banks look like floating shapes in the haze; eye height 100 makes the road look very wide (same as the original's proportions).
- R1.4: opponent (and the police car, which takes over the opponent's pictures in police_spawn): the largest picture of the view the original chose (DS:A7D4: side 0 lies along the road, quarters 1/3, rear 2 for 2 and 4..8; behind = mirror only), world size from the original's own picture size at its depth, smoothed. Scenery: `front_objects` (6a32) rules evaluated once per track into an object list (segment, lateral x, SR2_SHP type, side); each shape's largest LOD rasterised from its spans, world scale = median of (LOD height x depth z / 4); camera-facing, batched per type. Simplified: fast machine, bush group = one bush, no sign pictures (52e2), no guard rails / truss bridges / tunnels (Mulholland 5404 / 555e), no drag-strip walls (R1.6). Checked headless: tree-lined town avenue, Mulholland poles / posts / bushes, blue opponent.
- R1.5: mirror glass = frame pixels (243,22)-(317,42) (measured; frame at x 240-242 / 318-319, rows 21 / 43), see-through via the mask fill; rear camera with the original's mirror scale (focal / 4, centre x 280, horizon row 36), flipped left-right; the opponent behind uses its largest front picture (views 9..12, size with the mirror's no-x4 projection). The mirror box exclusion is rows 20..44 plus the stem x 272..287 in rows 18..19 (mirror_draw_backdrop). Open (polish, R2.3): distant Mulholland banks show as pale blocks in the haze.
- R1.6: drag strip = flags L2/R2/C2[seg], geometry YAW[seg + 0xC0], flat (race3d.c accessors); barrier walls 50 high in 5-segment blocks colour 11 / 4 from DS:96F4; start tree = panel picture DS:9A40 (lamps composed in by the original each frame) cropped to its nearest size, sprite at start_seg + 4. The starter figure (a85c) is the 2D starter slide overlay, unchanged. Checked headless on Grudge Night.
- Why 3D straight away: the 2D renderer only gets ~6 position updates a second (physics every 12th tick, frames every
  8 ticks), so smoothness needs interpolation either way; doing it in 3D also makes rain and effects possible.
