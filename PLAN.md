# Street Rod 2 Enhanced — plan

## Status
Set up 2026-11-14 from the faithful port (`../street-rod-2` at 75326ff). Next: **R1.3**.

## User to-do
| What | Why |
|---|---|
| Play a little in the new OpenGL window: title, garage, a race; resize the window, Alt+Enter, mouse clicks land where the pointer is | R1.1 replaced SDL's renderer; it should look and click exactly as before |

## Phase R1 — 3D race view
The race view (front strip (0,18)-(319,101), mirror (240,22)-(319,42), `race_render.md` §1.3) is drawn by a GPU
renderer instead of the original road routines; the dashboard, cockpit overlay and status line stay the original
2D frame, drawn on top. Game state is read after each physics step and interpolated per displayed frame.

| Id | Task | Size | Status |
|---|---|---|---|
| R1.1 | Renderer base: SDL3 GPU (or OpenGL 3.3) context in `host.c`; the 320x200 frame becomes a texture drawn over a 3D viewport; `classic` switch keeps the 2D path; frame pacing at display refresh | M | done 2026-11-14 (user check pending) |
| R1.2 | State snapshot + interpolation: capture player seg/substep, lateral position, heading, speed, wheel, opponent / police positions at each physics step (race ISR, every 12th tick); blend the last two by elapsed time each frame | M | done 2026-11-14 |
| R1.3 | 3D road (modes 0, 1, 0x10): mesh from track arrays YAW/HGT/L/R/C (+BL/BR banks for Mulholland), road surface, lines, shoulders, sky and horizon; camera at the interpolated player position | L | |
| R1.4 | Sprites: opponent, police, roadside scenery (SR2_SHP shapes and LIB pictures) as billboards placed from the same data the 2D renderer uses | L | |
| R1.5 | Rear-view mirror as a second camera into the mirror rectangle | S | |
| R1.6 | Drag strips (modes 2, 4): barriers, start tree, starter figure | M | |
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
- Why 3D straight away: the 2D renderer only gets ~6 position updates a second (physics every 12th tick, frames every
  8 ticks), so smoothness needs interpolation either way; doing it in 3D also makes rain and effects possible.
