# Street Rod 2 — SDL3 port plan

Same approach as `StreetRod` and the earlier ports: a **faithful C port** here (reverse-engineer →
document → port function by function, original data read at run time), then, once it is
published, a separate **Street Rod 2 Enhanced** repo built on its release. Token efficiency is a first-class
goal: small subtasks, one session each, the cheapest model that can do the job, and the user doing
whatever is cheap for a human and expensive for a model (looking at pictures, playing the game,
driving DOSBox).

**The big lever:** *Street Rod 2: The Next Generation* (1991) is built on the *Street Rod* (1989)
engine, which is already reverse-engineered, specified and ported in `../StreetRod` (faithful,
v0.1.0) and `../StreetRodEnhanced`. So this plan is a **delta port**: match every SR2 function
against SR1 (S1.3), carry names and spec sections over for the identical ones, and spend sessions
only on what changed or is new (AdLib music, `LIB3`, `SR2_*` data, new screens and rules). The
port starts as a copy of `../StreetRod/srport` at its `v0.1.0` tag (S4.1), not from scratch.

## User to-do

What the agents need from you now, most urgent first. Agents add a row when they hand you a task and
remove it when it is done; subtasks marked `U<n> pending` are added automatically on the plan page.

| Task | What to do | For | Time |
|---|---|---|---|
| 3D race view | Try `--rain`, `--dusk`, `--night` (and `--rain --night`): rain, windscreen drops, spray, headlights | R2.1 - R2.3 | 10 min |
| 3D race view | Drive a cruise and a Mulholland race in the 3D view: smooth? road where you expect it, curves and hills feel right? (`--classic` = old view) | R1.2 / R1.3 | 10 min |
| 3D race view | Play a little in the new OpenGL window: title, garage, a race; resize the window, Alt+Enter, mouse clicks land where the pointer is | R1.1 replaced SDL's renderer; it should look and click exactly as before | 10 min |

## Status

- **3D race view (R1) and effects (R2):** done 2026-11-14 (built in a temporary clone, merged back here the same day). Game logic unchanged; `--classic` = the original 2D race view; `--rain`, `--dusk`, `--night`.
- **Done:** S0.1 (repo), S1.1–S1.4 (unpacked, indexed, matched with SR1, Ghidra: `port/RE_GUIDE.md` §1–2); S1.5 (what changed, `RE_GUIDE.md` §3); S2.3, S2.5 (`FORMATS.md`); S2.1, S2.2, S2.4 waiting for your U5 checks. Phase 3: all delta specs (`port/spec/*.md`, 913/913 functions, `tools/sr2_coverage.py`).
- **Next:** v0.1.0 is published (https://github.com/kylofon/street-rod-2-sdl3/releases/tag/v0.1.0). Your playtests (User to-do) → S5.6 fix batch(es) → v0.1.x patch release. When the playtests pass, the faithful port is finished and the Enhanced repo can start (game-port-setup mode B).
- Escalation rule: a subtask that fails twice on Sonnet → new Opus session with a 5-line note
  (symptom, file, what was tried). Never carry an old transcript over.

## Model guide

| Model (app model picker) | Effort | Use for | Relative cost |
|---|---|---|---|
| **Haiku** | low–medium | Running and adapting existing tools, regenerating outputs, symbol merges, copying files, coverage checks, packaging, FORMATS/PLAN bookkeeping | 1× |
| **Sonnet** | medium | Default: indexers, decoders for simple/known formats, delta specs, ports of changed shell/UI/driver code, most C porting | ~3× |
| **Opus** | high | Unknowns: changed race physics/AI, renderer changes, cross-module bugs, escalations | ~5× |

Subagents inherit the session's model; wide searches go to the `scout` agent (always Haiku).

## How to run a subtask (user, every time)

1. Code tab → **New session** in `C:\Coding\StreetRod2` (never continue an old session for a new
   subtask: the old context is re-billed on every turn).
2. Pick the model from the subtask table in the model picker; set the effort.
3. Send exactly: `Do S1.1 from PLAN.md.` (with the subtask id). `CLAUDE.md` tells the agent what to read.
4. When the agent says it is done: glance at the commit (`git log -1 --stat`), do any U-check it asks
   for, then close the session. Token usage per subtask: `site/index.html` (`python tools/plan_site.py`).

## What we have (recon, 2026-10-09)

`Game/` holds the installed game (git-ignored; the port reads it at run time). `_tools/` is a
junction to `C:\Coding\TestDrive1987\_tools` (Ghidra, JDK). Previous engine: `../StreetRod`
(`port/RE_GUIDE.md`, `port/spec/*.md`, `port/symbols.csv`, `FORMATS.md`, `srport/`).

| File(s) | Size | Notes |
|---|---|---|
| `SR.EXE` | 264 468 | EXEPACK as SR1 (`tools/unexepack.py` → 288 912 bytes, entry 34b0:0018, 2721 relocs, DGROUP 3E08; 34 code segments, 863 functions). "MS Run-Time Library - Copyright (c) 1990" → **MSC 6.0** (SR1: MSC 5.1), so runtime functions will not byte-match SR1; game code shares the source (S1.3: 491 of SR1's 769 functions found). Same video menu as SR1 (CGA / EGA / VGA 320×200×16 / Hercules / Tandy), new "STREET ROD II: Sound Options" menu (AdLib, Tandy, speaker). Switches seen: `nomouse`, demo mode |
| `LIB1`, `LIB2` (+`C`) | 43 K / 306 K (21 K / 268 K) | picture libraries, SR1 family (`tools/srlib.py`); `C` = 2-bit CGA variants |
| `LIB3`, `LIB3C` | 144 K / 113 K | **new** third picture library (SR1 had two) |
| `SR2_SHP` | 42 742 | ? shapes (sprites / road objects), read with a "Data disk read fail" check |
| `SR2_DATA` | 3 607 | ? game data, SR1 `HOT_DATA` successor (opponents, cars) |
| `SR2_END` | 1 236 | ? ending / "End Data" |
| `MSR.SND` | 1 956 | AdLib timbre bank (instrument names `piano1`, `jazzguit` …; AdLib Visual Composer `.SND` layout) |
| `MSRBEG.MUS`, `MSR2/20/21/24.MUS`, `PAUSE.MUS` | 7 K, 10 K, 1–5 K, 101 | AdLib Visual Composer `.MUS` songs (title, in-game, pause). **New:** SR1 was PC speaker only |
| `INSTALL.BAT` | 1 746 | 4-disk installer to `C:\SR2`; no program chain: one executable |
| `*.exo` | 0 | eXoDOS marker |

No duplicate files (hash-compared); no data-disk variant like SR1's. Copy protection: expected as in
SR1 (car-key colour question) — S1.5 confirms.

## Subtasks

Model: H = Haiku, S = Sonnet, O = Opus. Last column: size (S/M/L) until done, then the status
(`done YYYY-MM-DD`, `U5 OK 2026-10-19`). Notes go in a "Notes:" list under a table, never between rows.

### Phase 0 — Repository
| Id | Task | Model | Size |
|---|---|---|---|
| S0.1 | Repo, junction, tools copied from `F117-2` (generic) and `StreetRod` (engine-specific), PLAN/CLAUDE/README, plan page | O | done 2026-10-09 |

### Phase 1 — Executable map (output: `port/RE_GUIDE.md`, `port/sr2_functions.csv`, `port/symbols.csv`)
| Id | Task | Model | Size |
|---|---|---|---|
| S1.1 | `tools/survey.py` on `SR.EXE` → `work/survey.txt`; identify the packer (try `tools/unexepack.py` first, else `unlzexe.py`/a new unpacker) → `work/SR2_unp.exe`, entry, relocs, DGROUP; summary into `RE_GUIDE.md` §1 | H | done 2026-10-09 |
| S1.2 | Indexer `tools/sr2index.py` from `srindex.py`: segments, functions, DGROUP, strings, ints, ports → `port/sr2_functions.csv/.json` | S | done 2026-10-09 |
| S1.3 | **SR1 match**: `tools/sr2match.py` (code, strings, DGROUP globals, call graph, link order) → `port/sr2_sr1_matches.csv` with class `same` / `changed` / `new`; SR1 names carried over → `port/symbols.csv`; counts per SR1 module into `RE_GUIDE.md` §2 | O | done 2026-10-09 |
| S1.4 | Ghidra: adapt `tools/ghidra/sr2_*.sh` (from the F117 scripts), `SetDS` + `ApplySymbols` + `DecompileAll` headless in the background → `port/decomp/` (ignored); print only counts | H | done 2026-10-09 |
| S1.5 | Map of what changed (also: SR2 seeds for `sr2index.py` — handlers, graphics vectors; MSC 6 runtime names via `apmatch.py`): segment map, main state machine vs SR1, file loading (`LIB3`, `SR2_*`), sound-device dispatch (AdLib / Tandy / speaker), copy protection, switches → `RE_GUIDE.md` §3; module of every `changed`/`new` function (`tools/assign_modules.py`) | S | done 2026-10-09 |

Notes:
- S1.5: index now 913 functions (85 %, ~96 % without the 28 KB data block 3725:0000), matcher same 56 / changed 442 / new 415, 498 of 769 SR1 functions; `port/sr2_modules.csv` (`tools/sr2_modules.py`, new functions by neighbour vote — first guess); `port/symbols.csv` 470 names (`tools/sr2_symbols_from_matches.py`). New segments: 2925 game (tournament, ending, stickers), 2c57 sound/MUS, 2f1b music timer.
- S1.4: `bash tools/ghidra/sr2_ghidra.sh` (`decompile` = skip the import): 855 functions decompiled, 0 failed; pcode warnings in 2a21:4806, 3c57:1bc5/2875, 471c:0000/0012, 4722:0000 (Ghidra segments). Output `port/decomp/sr2.c`, `sr2_ds.c`, `sr2_globals_xref.txt`.
- S1.3: MSC 6 rebuild means few byte-identical functions (56 `same`); `tdmatch.py` alone found 115. ~95 % of evidence-bearing pairs are callee-consistent; treat `order`/`calls` matches as strong hints, confirm in the specs.

### Phase 2 — File formats (output: `FORMATS.md`, decoders in `tools/`, PNG/WAV in `work/`)
| Id | Task | Model | Size |
|---|---|---|---|
| S2.1 | `LIB1`/`LIB2`/`LIB3` through `tools/srlib.py` (extend for differences) → PNG + contact sheets; **U5 check** | S | U5 OK 2026-10-19 |
| S2.2 | `SR2_SHP` → PNG frames + contact sheet; **U5 check** against U3 captures | S | U5 OK 2026-10-19 |
| S2.3 | `SR2_DATA`, `SR2_END`, exe tables (cars, parts, opponents, town, texts) via `tools/srtables.py` adapted → `FORMATS.md` | S | done 2026-10-09 |
| S2.4 | Sound: `MSR.SND` bank + `.MUS` songs (AdLib Visual Composer formats), the game's AdLib driver/tempo, render via Nuked-OPL3 (`../F117-2/third_party/nuked-opl3`) → `work/sound/*.wav`; speaker-music deltas vs SR1 (`tools/srsnd.py`); **U5 listen** | S | U5 OK 2026-10-19 |
| S2.5 | Save games → `FORMATS.md` (diff against SR1's) | H | done 2026-10-09 |

Notes:
- S2.2: `SR2_SHP` = 26 vector span-shape sets (7 LODs each), `tools/sr2shp.py`. S2.3: `SR2_DATA` 9 blocks + text pool (22 cars, 22 people), `SR2_END` ending animation script, exe tables (parts catalogue DS:4502, 64 parts / 8 categories), `tools/sr2data.py`. S2.5: `HOTROD0..14.SAV`, layout vs SR1 in FORMATS.md. Open: engine-bay tables DS:47A8–4EB4, DS:9B40 block, car flag word +18, SR2_DATA block 7 bitmaps.
- S2.1: SR2 directory records are 28 bytes (FORMATS.md); LIB1 4, LIB2 212, LIB3 83 pictures, CGA variants too.
- S2.4: `tools/sr2snd.py` (Ad Lib toolkit player re-implemented); speaker-music deltas still open (S3.2). The bank also carries AdLib sound effects.

### Phase 3 — Delta specs (output: `port/spec/<module>.md`, gate: `tools/sr2_coverage.py` 100 %)
SR1 modules and specs (`../StreetRod/port/spec/`) are the base. Per module: `same` functions get a
one-line reference to the SR1 spec section; only `changed` and `new` functions get full text. One
session per row; written so that a Sonnet session can port a function from its section alone.
| Id | Module | Model | Size |
|---|---|---|---|
| S3.1 | platform + video: MSC 6.0 runtime, startup, menus, timer, input, files, graphics library deltas | S | done 2026-10-09 |
| S3.2 | sound: AdLib driver + `.MUS` sequencer (new), speaker/Tandy deltas | S | done 2026-10-09 |
| S3.3 | game_flow: title, calendar, money, save/load, endings (`SR2_END`), demo | S | done 2026-10-09 |
| S3.4 | garage: shops, parts (incl. automatic transmissions), newspaper, customising | S | done 2026-10-09 |
| S3.5 | race: physics, gear box, damage, police, opponent AI deltas | O | done 2026-10-09 |
| S3.6 | race_render: road, cockpit, `SR2_SHP` objects | O | done 2026-10-09 |
| S3.7 | Coverage gate, symbols merge, `port/symbols.csv` final | H | done 2026-10-09 |

Notes:
- S3.1–S3.6 ran as parallel agents (race_render in 5 parts, merged); S3.7: `tools/sr2_dedupe.py` (one owner per function: SR1 module for matched, fullest text for new; second copies marked "Secondary view"), 18 gap functions added, `tools/sr2_merge_symbols.py` → `port/symbols.csv` 913 functions + 654 globals, 0 clashes.
- Findings: road modes `DS:ADD6` 0 cruise, 1 street drag, 2/4 Grudge Night strip, 8 Aqueduct, 0x10 Mulholland; integer physics, 64 sub-steps per segment; 20-slot driver table with polygon fill; LIB unpack escape `00 00`/`FF 00` (fixed in `tools/srlib.py`); playlists MSRBEG,MSRBEG,PAUSE,MSR2,PAUSE,MSR24,MSR24 / MSR21×2, MSR20 unused; `tools/sr2snd.py` should adopt sound.md §7 items 1, 3, 5–11.
- Suspected original bugs to reproduce (check in DOSBox when convenient): zero-width road on roadworks segments, hill-peak table overrun, uninitialised sprite width in af98 (race_render.md); stock-car garbage fields (race.md).

### Phase 4 — `sr2port/` skeleton
| Id | Task | Model | Size |
|---|---|---|---|
| S4.1 | `git -C ../StreetRod archive v0.1.0 srport` → `sr2port/` (no build dirs, no launcher); rename targets to `sr2port`; loader unpacks SR2's exe (S1.1 result); code pointers re-based on SR2 addresses; placeholder `game_main` shows `LIB1` #0; headless check runs | S | done 2026-10-09 |
| S4.2 | Generated `symbols.h` from `port/symbols.csv` (`tools/gen_symbols.py`), module headers (`gen_module_headers.py`), `sr2port/PORTING.md` deltas | H | done 2026-10-09 |

Notes:
- S4.1: `sr2port/` from SR1 `v0.1.0` (host, mem, codeptr, EGA/VGA model); SR1 game/platform modules dropped (they use SR1 addresses) and re-copied per S5 subtask. New `platform/lib.c` (28-byte LIB records, unpack escape), placeholder `game_main` (LIB1 #0). Build: `cmake -S sr2port -B sr2port/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc` with `C:\msys64\mingw64in` on PATH; 0 warnings; `sr2port --check`; headless `SDL_VIDEO_DRIVER=dummy SR_SNAPSHOT_DIR=work/s41snap sr2port --run-ms 600` → BMP snapshots. Nuked-OPL3 in `third_party/` (unused yet).
- S4.2: `tools/gen_symbols.py` (SR1 version) → `sr2port/src/symbols.h` (913 FN_, 654 DS_); no module-header generator; `sr2port/PORTING.md`.

### Phase 5 — Port the deltas, module by module
Same functions keep the SR1 code (re-pointed at SR2 data addresses); changed/new ones are ported from
the delta spec. Each row ends with a **U6 playtest** request.
| Id | Task | Model | Size |
|---|---|---|---|
| S5.1 | platform + video deltas → title and menus (**first visible milestone**) | S | U6 OK 2026-10-19 |
| S5.2 | game_flow + garage deltas, `LIB3`, save/load | S | U6 OK 2026-10-19 |
| S5.3 | race + race_render deltas, `SR2_SHP` | O | U6 OK 2026-10-19 |
| S5.4 | sound: AdLib `.MUS` player on Nuked-OPL3 (`third_party/` from F117-2), sound options menu, speaker deltas | S | U5/U6 OK 2026-10-19 |
| S5.5 | endings, demo mode; headless demo chain through every screen | S | done 2026-10-10 |
| S5.6 | U6 bug batch (repeat as needed) | S | batch 1 done, more U6 open |

Notes:
- S5.1: platform 56 + video 81 functions ported (+ title_sequence, pools_init, rnd, input helpers from game_flow); stubs in `game/flow_stub.c` and sound hooks in `modules.c`. PORT: text menus drawn in an 80×25 text mode (frame up to 640 wide, game font), far heap = one block per `_fmalloc`, EGA runs as VGA, CGA/Hercules/Tandy refused. Untested yet: ega_anim_step, ega_slide_sprite, ega_blit_shifted, cursor race branch, status_print/money_add. Spec fixes found: step table at DS:3B54/3B5A, DS:1BCC/1BCE is a far pointer.
- S5.2: game_flow (~85 functions with the UI toolkit) and garage (~75) ported in `sr2port/src/game/` (flow_*, ui_*, garage_*, event15/17); races stubbed in `race_stub.c` (`/* PORT: S5.3 stub */`), sound no-ops in `snd.h`/`ds_screens.h`. Dev aids: `SR_SEED=n`, `SR_TRACE=1|2`. Save file 708 bytes = FORMATS.md layout. Demo runs through every garage code for 580 s. Open: tires / sell parts open a message box instead of a list in the scripted run; spec fixes: walker_anim 8 args, DS:1F3E/1F40, panel_wipe arena_pop(1) for even modes.
- S5.3 (in progress, 2026-10-09): ~75/84 race + ~90/134 race_render functions in `sr2port/src/game/race_*.c`; driver slots 13–15, 18, 19; road modes 0, 1, 2/4, 0x10 work headless (drag lost/home, Mulholland won/home, Grudge time run + 2 rounds, cruise arrives, 700 s demo without fatal). 2026-10-10: Aqueduct (mode 8) ported in `race_aq.c` incl. slot 16 rotation; all five race kinds run headless (Aqueduct won/lost/wall crash), 300 s demo without fatal. Dev aid `SR_DEBUG_RACE=<mode>`. Spec errors: bracket start 7 ticks per tenth (not 13); crash_show plain blit; clock hands 9/16; tires_replace reads list[1] and writes DS:0000 (original bug).
- S5.4 (2026-10-10): `sr2port/src/sound/` (spk.c, adlib.c, mus.c, adfx.c): 25-slot table, speaker sequencer, AdLib driver + MUS playlists + music ISR (game tick 68.6 Hz during music, U7 #6), effects; Nuked-OPL3 mixed in host.c at 49716 Hz; dev aid `SR_AUDIO_WAV=path`; `tools/wav_rms.py`; `tools/sr2snd.py` follows sound.md §7. Fixed on the way: reject beep is slot 12 (not 15) in ui_edit.c; race ISR enter/leave sound globals; DS:0050 stays 666 until sound init. Open: title music stops after the first song (~136 s gap reproduced, `PORT: verify U5`).
- S5.4 fix (2026-11-14): guitars / bass fell silent after their first note: the driver retriggers with key-off + key-on back to back, and Nuked-OPL3 (cycle-accurate) missed the key-off at the same sample. host.c now uses `OPL3_WriteRegBuffered` (writes 2 OPL samples apart, ~real AdLib write time). `work/oplrender.exe` (renderer for `tools/sr2snd.py`) needs the same buffered writes.
- S5.5 (2026-10-10): `tools/sr2_smoke.py` (15 headless paths on copies of Game/ in work/s55game, all ok: title→classifieds, 420 s demo, garage tour, save→load, jail, all race kinds, King/summer/broke endings); dev aids `SR_DEBUG_END=king|summer|broke|garage`, `SR_DEBUG_HOURS`, `SR_DEBUG_GAS`, `SR_DEBUG_RACE=jail`. Fix: jail decoration loop drew an 8th garbage entry. Disassembly cross-checks (objects, mirror bands, town walls, bob_hotspots, ending) match. Spec error: no msg 0x44 after the ending.
- S5.6 batch 1 (2026-10-10): race status strip showed the raw protection format — `gfx_screen_mode(3)` passed DS:4846 instead of the string it points to (" Gear: "); fixed, platform.md corrected. Launcher `Street Rod 2.exe` (from SR1, settings in %APPDATA%\Street Rod 2, `--print-command`); port switches `adlib`/`speaker` (`/* PORT: launcher */`); traces `SR_TRACE=s|t`, `SR_DEBUG_PROT=1`. Smoke test 15/15 ok. Launcher then restructured after `../AcesOfThePacificEnhanced/launcher` (wxFrame + wxSimplebook pages, menu bar, read-only Keys page, Preferences; Enhanced-only pages left out; patterns listed in `sr2port/launcher/README.md`).

### Phase 6 — Release
| Id | Task | Model | Size |
|---|---|---|---|
| S6.1 | Packaging script (from `../StreetRod`), README, CHANGELOG, v0.1.0 zip; **U6 playtest** of the zip | H | done 2026-10-10 (zip U6 open) |
| S6.2 | Publish: GitHub repo `street-rod-2-sdl3` + release (user approves), release-notes link in README | H | done 2026-10-10 |

Notes:
- S6.1 (2026-10-10): `sr2port/tools/package_release.py` → `release/sr2port-v0.1.0-win64.zip` (10.9 MB, 43 entries: sr2port.exe, Street Rod 2.exe, 19 DLLs from the objdump import closure, player README `sr2port/dist/README.md`, LICENSE, licenses/), `release/RELEASE_NOTES.md`, `SHA256SUMS.txt` (573a222a…); CHANGELOG.md; version 0.1.0. Unzipped run without msys64 on PATH: ok. No tag/remote yet.

Enhancements are done in this repo (user, 2026-11-14), each behind a switch with the original as fallback; the
game logic stays the faithful port.

## Phase R1 — 3D race view (enhancement, behind `--classic`)
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
| R1.7 | Aqueduct (mode 8): concrete channel mesh from the AQ flags, roll, rotating opponent sprites | L | done 2026-11-14 |
| R1.8 | Crashes, cliff fall, breakdowns, Bob's Drive-In / jail scenes: check each against the 2D path | M | done 2026-11-14 |

## Phase R2 — effects
| Id | Task | Size | Status |
|---|---|---|---|
| R2.1 | Rain: particles, streaks on the windscreen, wet road (darker, reflections) | M | done 2026-11-14 (no reflections yet) |
| R2.2 | Spray and tyre smoke behind cars | S | done 2026-11-14 |
| R2.3 | Fog / dusk / night lighting with headlights | M | done 2026-11-14 |

Notes:
- R1.1: `src/r3d/r3d.c` (OpenGL 3.3 core, GL loaded via SDL_GL_GetProcAddress, frame = BGRA texture, 4:3 letterbox, vsync); `host.c` presents through it and maps the mouse with `r3d_letterbox`; no context (SDL_VIDEO_DRIVER=dummy) = not shown, game runs (smoke tests unchanged); `--classic` parsed in main.c (no effect until R1.3).
- R1.2: the view is captured per original frame (road_frame, ~9 Hz: every way the game moves the camera is covered) and blended one frame behind (`r3d.c` view_now). 3D mode keeps the original 2D drawing (side effects), then `game/race3d.c` repaints the view twice (colour 0 / 15) under the overlays: unchanged pixels = overlay or mirror, the rest see-through (`ega_set_page_mask`, alpha in compose). Test scene in `r3d/scene.c`. Checked headless (`SR_HIDDEN=1`, snapshots read back from the off-screen buffer; `SR_R3D_TRACE=1` prints the blended view): 110 fps unpaced, speed per frame within ±10 %, no stalls.
- R1.3: `r3d/scene.c` builds 160 segments ahead per frame (original: 15). Scale from the original projection: 1 depth unit = 64 lateral units (focal 256 frame px, ~64° view), segment 512, YAW unit 1/256 rad, HGT unit 30, eye 0x7D0 - cam_y above the road; anisotropic projection on the 320x84 view strip, so near geometry lines up with the 2D view. Track data (yaw -756..280, hgt -10..140, banks +-4000) pushed by race3d.c each original frame (copied when changed). Road / shoulder / grass bands, centre dash (C & 8), Mulholland banks (hill / drop-off), distance fog. Checked headless on cruise and Mulholland. Open: distant banks look like floating shapes in the haze; eye height 100 makes the road look very wide (same as the original's proportions).
- R1.4: opponent (and the police car, which takes over the opponent's pictures in police_spawn): the largest picture of the view the original chose (DS:A7D4: side 0 lies along the road, quarters 1/3, rear 2 for 2 and 4..8; behind = mirror only), world size from the original's own picture size at its depth, smoothed. Scenery: `front_objects` (6a32) rules evaluated once per track into an object list (segment, lateral x, SR2_SHP type, side); each shape's largest LOD rasterised from its spans, world scale = median of (LOD height x depth z / 4); camera-facing, batched per type. Simplified: fast machine, bush group = one bush, no sign pictures (52e2), no guard rails / truss bridges / tunnels (Mulholland 5404 / 555e), no drag-strip walls (R1.6). Checked headless: tree-lined town avenue, Mulholland poles / posts / bushes, blue opponent.
- R1.5: mirror glass = frame pixels (243,22)-(317,42) (measured; frame at x 240-242 / 318-319, rows 21 / 43), see-through via the mask fill; rear camera with the original's mirror scale (focal / 4, centre x 280, horizon row 36), flipped left-right; the opponent behind uses its largest front picture (views 9..12, size with the mirror's no-x4 projection). The mirror box exclusion is rows 20..44 plus the stem x 272..287 in rows 18..19 (mirror_draw_backdrop). Open (polish, R2.3): distant Mulholland banks show as pale blocks in the haze.
- R1.6: drag strip = flags L2/R2/C2[seg], geometry YAW[seg + 0xC0], flat (race3d.c accessors); barrier walls 50 high in 5-segment blocks colour 11 / 4 from DS:96F4; start tree = panel picture DS:9A40 (lamps composed in by the original each frame) cropped to its nearest size, sprite at start_seg + 4. The starter figure (a85c) is the 2D starter slide overlay, unchanged. Checked headless on Grudge Night.
- R1.7: the original's Aqueduct is straight and flat (aq_project_front uses the heading only), so the 3D channel is straight: floor 0x5C8..0xCF8, walls to 0x438 / 0xE88 at 340 above, plus the per-frame deformation a094 sets (DS:985E, A2B8, A7A6, A5FA, 9A5A, 9B14), camera x clamped 0x564..0xD5C, roll = atan2(DS:A540, DS:BA90). Flags 3726:4E62: joint lines, trough, wall openings, white bands. Opponent = the original's chosen (rotated) picture. Not done: pillars (0x80), low box (0x10), SR2_SHP format-B objects (0x20 / 0x40). Checked headless.
- R1.8: page masks now also need the page's view area unchanged since the mask was made (checksum in ega.c), so a crash picture or the next screen drawn on that page shows whole. Bob's Drive-In run checked headless (town cruise 3D, the bungalow screen whole, the race 3D). `SR_SMOKE_3D=1 python tools/sr2_smoke.py` runs the smoke tests with the hidden OpenGL window: all 8 race tests ok with the 3D path, same timings as 2D; full suite (2D path) 15/15 ok.
- R2.1: `--rain` (`r3d/rain.c`): 2500 world streaks in a 10000-unit box around the eye (fall 2500 units/s, slanted by the car's smoothed velocity), windscreen drops in screen space (drawn after the 3D scene, under the 2D cockpit), overcast sky, fog from 2500 over 30000, colours x0.72. Mirror gets the streaks, not the drops. Not done: road reflections.
- R2.2: puffs (`rain.c`): soft camera-facing discs that grow, drift and fade; spray from a moving opponent's rear corners in the rain (60/s), smoke from our rear wheels while DS:72E0 (squeal) is set (30/s, seen mostly in the mirror); time-based emission.
- R2.3: `--dusk` (orange sky, ambient 0.62) and `--night` (dark sky and fog, ambient 0.12, headlight cone ahead: half-width 0.42 x distance + 500, fading to 14000) in both shaders, `fwd` = the car's heading (also in the mirror pass); rain and smoke dimmed with `r3d_fx_set_light`. Combine with `--rain`.
- Fix 2026-11-14 (user report: turning reversed, colours wrong): the original shifts a row by heading - YAW to the right, so a larger YAW bends the road left and a larger heading looks left (both signs flipped in scene.c). Colours now by palette index through the live palette registers (R3dView.pal): road 1 (finish 15), grass 9 next to the road, dash 4, Mulholland kerbs 2, banks 14, pavements 4, walls 11/4, sky 0, Aqueduct floor 5. Sky = the original's panorama from page 1's storage rows (normal: rows 102..153 + 163..182, horizon row 64; Mulholland: rows 110..199, horizon 44) on a cylinder (1 px = 1/256 rad), ground colour 9 below; no haze by day (the original has none). Compared side by side with `--classic` headless. Gantries done next (below).
- Town fix 2026-11-14 (user report: houses / overhead signs missing): scenery only in the original's range (front_objects rows < 11: 12 segments ahead, 8 in the mirror; no more tree walls hiding the horizon panorama); intersection pavements (colour 4, half grass where L / R & 0x80, kerb lines 2); gantries (banner 55ce, C & 0xF000, modes 0 / 1): posts and bar in colour 10 at 400 up, sign = shape 0x0C / 0x0D / 0x0E or a sign picture (52e2: billboards DS:96AE, location sheet in page 0 rows 100.., size 6 of DS:7F02, 7 world units / px); corner location signs at intersections (L / R & 1). Gantry signs are drawn as far as the gantry. Smoke race tests same timings 2D / 3D.
- Why 3D straight away (user, 2026-11-14): the 2D renderer only gets ~6 position updates a second (physics every 12th tick, frames every
  8 ticks), so smoothness needs interpolation either way; doing it in 3D also makes rain and effects possible.

## Enhanced backlog
Ideas collected while porting. One row each; they become the Enhanced repo's subtasks. Seeded from
what `../StreetRodEnhanced` already does for the shared engine.
| Id | Idea | Model | Size |
|---|---|---|---|
| E1 | Port Street Rod Enhanced's render back end: high resolution, smooth motion at the original draw distance (`../StreetRodEnhanced/ENHANCED.md` stage 1) | O | L |
| E2 | Longer draw distance (SRE stage 2) | O | M |
| E3 | Launcher from `../StreetRodEnhanced/launcher/`: game folder, sound device, switches, window options | S | M |
| E4 | Gamepad and key bindings, A/Z gear keys option | S | S |
| E5 | Widescreen / integer scaling options | S | M |
| E6 | Music option: no ~136 s silence between playlist songs (the original returns the next song's first delay 0 raw, so the countdown wraps; `sr2port/src/sound/mus.c` PORT note) | S | S |
| E7 | Amiga graphics option: unpack the Amiga release (`Game/amiga/disk1/libstart.p`, `disk2/lib.p`, packed; `tools/unadf.py` extracts the `.adf` disks), decode its pictures and palettes, map them onto the PC LIB1-3 screens | O | L |
| E8 | Smooth 2D animation: the retrace the game waits for locked to the display (every n-th refresh nearest 70 Hz, `host.c` vsync_wait), so 70 Hz frames do not judder on 60/120 Hz displays; `--classic` keeps the VGA timing (done 2026-11-14) | S | S |
Notes:
- E7 (2026-11-14): `libstart.p` (66 pictures) and `lib.p` (391) have the PC SR2 LIB directory (u16 count + 28-byte records), **big-endian**; depth 4/5/6 planes (1 for 3 pictures); offsets chain exactly to the file end, `size` is exact (PC: +1). Data has no `0xBC` marker; the PC run scheme (00/FF runs + 16 tokens) gives near but not exact sizes (31 of 457 exact), so the run table or row padding differs: read the unpacker in `STREET_ROD` (68000 hunk exe). First probe: `tools/amilib.py`. Palettes not located yet.
- E7 garage (2026-11-14, user request): `--amiga` shows Amiga lib #144 (the garage, 320x190, palette table 7) in place of LIB2 #18, in full colour: `enh/amiga.c` decodes it at start-up from Game/amiga (the user's own files), the garage hands over its composed backdrop as the reference, and the frame composer (ega.c apply_backdrop) shows the Amiga pixel where the screen still shows the backdrop and its 4 neighbours do too (so the name text, the car, highlights stay the game's; 1-pixel PC border around them); off when under 60 % of the area matches. Other Amiga pictures: same mechanism (Bob's = #120, gas station #142, jail #147, DMV licence #132 ...).
- E7 all (2026-11-14, user request): general mechanism: pic_get notes bitmap -> picture id (amiga_note_pic), the driver blits (drv_blit / drv_blit_masked) of a mapped picture onto a screen page add a region (ega_add_backdrop: page row / column, any page, so back-page drawing, copies and flips work; up to 32, newest on top, covered ones dropped); compose shows the Amiga pixel where the screen still shows the picture (4-neighbour rule, 60 % visible match). Map `enh/amiga_map.h`: 55 pictures (licence, police book, newspaper headers, classifieds, garage, engine screen, menu bar, load / save / forget, speech bubbles, tool icons, calendars, 20 portraits, board, ending text, wanted poster, race sky / horizon / Mulholland panorama / dashboards / A-pillar / starter / figures), candidates from `tools/amiga_map.py` (same size + shape), checked by eye; palette table per picture (7 town / menus, 1 race, 2 poster, 9 ending text). Left out: car body pieces and opponent cars (painted per car), Bob's / gas station / jail (no same-size PC picture). Regions are per page now (page = plane offset / 2000h; the race split screen shows page 0 under the view) and follow page-to-page blits; blits of other pictures drop the regions they cover. Race graphics taken out again: the Amiga dashboards / sky / panorama are the same art in other shades, and mixed with the game's gauges they left artefacts. Added by eye: Burger's Bungalow (1073 -> #120), gas station (1072 -> #142), King Street Rodders wall (1209 -> #114). Not mapped: the ending sunset (libstart #33: none of the 10 palette tables fits), the jail (no single PC picture). `SR_AMIGA_TRACE=1` lists the pictures each screen puts on a page.
- E7 cars (2026-11-14, user request "car sprites like the Amiga, a new palette swap"): the 22 Amiga bodies (lib.p #181, 185, 189, 193, 197, 201, 205, 209, 213, 217, 221, 225, 229, 232, 236, 239, 243, 247, 251, 255, 259, 263 = models 0..21) replace the PC body in car_compose (`amiga_car_body`): fitted bottom-centre into the PC frame (layout, wheels, driver, hot spots unchanged), background = colour 0 reached from the edge (flood fill; black inside the car stays), quantised to the live 16 colours with the paint ramp (table 7 entries 1..9 / 33..41) on the body's own two paint indices (the two most frequent PC body colours: garage regs 6 / 7, opponents 12 / 3). On screen (car_blit, amiga_car_shown after the parked copy and after a drive-in) the full-colour body: paint ramp pixels keep the Amiga brightness and take hue / saturation from the live light paint colour. Parts of the Amiga body outside the PC frame (the Mustang is 66 rows, the PC 56) are drawn as an overhang (ref 0xFE). Fix 2026-11-14 (user: "graphics apply randomly, the car is a mix of both styles"): blits of other pictures no longer drop regions (a dialog's background restore dropped the garage); the 4-neighbour rule only for pictures with text drawn on top (BD_STRICT), not for cars (it left a 16-colour ring); parked cars registered on both pages; one car region per page (BD_CAR); the driving animation registers the full-colour car per step (ega_anim_step -> amiga_on_sprite, frames 1..3 = base + 2 / 4 / 6 px); customisations use the Amiga pieces after each body (roof, rear, front bumper, placed where most of their pixels equal the body's), the PC pieces are skipped. Checked: the garage tour shows the Amiga car in every frame (driving in, parked, after dialogs / sub-screens). Open: the scoop (no Amiga piece found); more paint colours (paint shop + save, user to decide).
- E7 static assets (2026-11-14, user: "no dynamic conversion, package the graphics"): `tools/amiga_export.py` converts once into gfx/amiga (pic_<id>.bmp full colour; car_<m>.bmp with alpha 0 background / 254 paint / 255 other; car_<m>_<roof|rbump|fbump>.bmp; cars.txt piece positions); the game only loads those (next to the exe, else the working folder); `package_release.py --with-amiga-gfx` makes a private zip with them. gfx/ is git-ignored (game art). Fixes: the 16-colour fallback uses the paint registers (6/7 player, 12/3 opponents) instead of the two most frequent colours (black won: the black car); the drive-in keeps the animation's last full-colour car (copied to the other page when the animation ends) instead of a second registration; regions carried by page copies (pointer save-under) no longer evict the car (BD_COPY). Checked headless: drive-in, parked over clicks, the full garage tour.
- E7 rebuilt (2026-11-14, user: "build it properly", one folder): no overlay any more. `platform/shadow.c` keeps a full-colour twin of the EGA planes in step with every write in vga.c (latch and CPU copies carry entries, fills / text / changed pixels become the game's colour); `enh/amiga.c` is the drawing source around blits, the driving-car sprite and the car strip cache (pictures with a 32-bit gfx/pic_<id>.bmp, the fitted car body, RAM copies of the screen kept as checked blocks). gfx/ holds every picture: the Amiga version where tools/amiga_map.h has one, else the PC picture (8-bit); cars, pieces, cars.txt. Checked headless: garage drive-in (71 frames), 5-min demo tour.
- E7 robustness (2026-11-14, user: "still randomly old vs new"): tested with the 5-minute demo autopilot (used cars, gas station, garage, tyre change, spare parts, under the car, engine screens). Fixed: page copies carry a region only when they take >= 90 % of it (the animation's background repairs made hundreds of scraps that pushed the garage out of the list); the on-screen test is per 16 x 16 tile (a dialog switches off only the tiles under it; single-colour tiles follow the region, which needs a third of its patterned tiles); the gas station / jail tile animations register the car too (pumps / bars stay in front).

## User tasks (cheap for you, expensive for a model)

### U1 — Manual and docs (now, 10 min)
The manual answers game-rule questions that would otherwise cost reverse-engineering sessions.
1. Find the manual / reference card PDFs (eXoDOS: `eXoDOS\Manuals\MS-DOS\Street Rod 2 The Next
   Generation (1991).pdf`, or the Internet Archive).
2. Save them as `C:\Coding\StreetRod2\docs\manual.pdf` (and `keys.pdf`). PDFs and text are git-ignored.
3. With Poppler: `pdftotext -layout docs\manual.pdf docs\manual.txt`, so agents grep text instead of
   reading the PDF. Otherwise skip; an agent will do it once.

### U2 — DOSBox-X reference setup (now, 15 min)
1. Download DOSBox-X (portable zip, Windows 64-bit) from https://dosbox-x.com and unzip it to
   `C:\Coding\StreetRod2\DOSBOX\` (git-ignored), or copy `..\StreetRod\DOSBOX\` if you still have it.
2. Create `C:\Coding\StreetRod2\DOSBOX\game.conf`:
   ```ini
   [sdl]
   output=opengl
   [dosbox]
   machine=svga_s3
   memsize=4
   captures=C:\Coding\StreetRod2\work\ref
   [cpu]
   core=normal
   cputype=386
   cycles=fixed 8000
   [sblaster]
   sbtype=sb16
   oplmode=opl2
   [speaker]
   pcspeaker=true
   [autoexec]
   mount c C:\Coding\StreetRod2\Game
   c:
   ```
3. Create the folder `C:\Coding\StreetRod2\work\ref`.
4. Run `DOSBOX\dosbox-x.exe -conf DOSBOX\game.conf`, then `SR`: choose **3 – VGA** and **AdLib**.
5. If the game runs too fast or slow, change `cycles` and note the value in a reply.

### U3 — Reference captures (whenever, 30 min; more on request)
Screenshot hotkey: **Ctrl+F5** (if nothing appears in `work\ref`, check the DOSBox-X mapper,
Ctrl+F1). Rename the files to `NN_name.png`:
`01_video_menu`, `02_sound_menu`, `03_title`, `04_protection`, `05_newspaper`, `06_used_cars`,
`07_garage`, `08_parts_shop`, `09_engine_bay`, `10_paint`, `11_town_drive`, `12_drive_in`,
`13_drag_race`, `14_road_race`, `15_police`, `16_save_menu`, `17_ending` (if you get there).
Also two AdLib music samples: **Ctrl+Alt+F7** starts/stops a raw OPL capture (`.dro`): the title
music → `title.dro`, and the pause music → `pause.dro`. These are the ground truth for U5; agents
only open them when a check fails.

### U4 — Memory dumps (only when an agent asks)
An agent gives you a moment and optionally a breakpoint.
1. Run DOSBox-X as in U2 and reach that moment.
2. Open the debugger: menu **Debug → Debugger** (or Alt+Pause).
3. If a breakpoint was given: `BP SSSS:OOOO`, Enter, then F5 and play until it stops.
4. `MEMDUMPBIN 0000:0000 A0000`, Enter. It writes `MEMDUMP.BIN` in the DOSBox-X folder.
5. Move it to `work\ref\dumps\<name the agent gave>.bin` and reply "dumped".

### U5 — Visual / audio checks (when an agent asks)
Open the file it names (PNG, contact sheet, WAV) next to your U3 capture and reply in one line: `OK`,
or what is wrong and where ("colours shifted, red where blue", "frame 3 offset 8 px right", "music
too fast"). Agents do not look at images themselves.

### U6 — Playtests and bug reports (from phase 5)
Build: `cmake --build sr2port\build --config Release`, run as the agent says. Report:
```
Build: <commit hash>     Screen: <which screen>
Steps: <what you did, from launch>
DOSBox does: <expected, or capture name>
Port does: <what happened; crash message if any>
Files: work\ref\bugs\<n>.png, save file copy if relevant
```
Several bugs in one message; one session then fixes them in a batch.

### U7 — Decisions (answer when convenient; defaults apply otherwise)
1. **Graphics**: VGA 320×200×16 only, as in the SR1 port; CGA/EGA/Tandy/Hercules → TODO. Default: yes.
2. **Sound**: AdLib (Nuked-OPL3) first, PC speaker as in SR1 second (S1.5: SR2 has no Tandy sound, only these two). Default: yes.
3. **Copy protection**: S1.5 found the manual-word question never checks the answer (always passes). Keep the question screen as in the original, or skip it? Default: keep (faithful).
4. **Shared code**: copy `srport` code into `sr2port` (independent repo, fixes ported by hand) rather
   than a shared library with SR1. Default: copy.
5. **GitHub**: published 2026-10-10 as https://github.com/kylofon/street-rod-2-sdl3 (public), release v0.1.0.
6. **Game tick while AdLib music plays**: the original runs the game ISR at 68.6 Hz (tempo 120) / 60 Hz (tempo 60) instead of 72.8 Hz, because the music ISR chains it with the remainder discarded (sound.md §9). Reproduce it (faithful)? Default: yes.

## Decisions
- 2026-11-14 (user): the copy protection question is skipped (it accepts any answer); `--protection` shows it. Its surroundings (black screen, palette, flags, the rnd(24) draw) still run, so the random sequence is unchanged (`game/flow_end.c`). Smoke: `title_skip` (default) and `title_new` (with --protection).
- 2026-11-14 (user): the 3D race view and the effects live in this repo, not a separate one (the temporary `street-rod-2-enhanced` clone was merged back). Game logic stays faithful; `--classic` = original races.
- 2026-11-14 (user): no start-up video and sound menus: without switches the port starts in VGA with AdLib (`platform/startup.c`, `PORT: user decision`); the switches still choose. Replaces the 2026-10-10 rule below that keeps the text menus.
- 2026-10-10 (user): launcher in the faithful repo, like the SR1 port: video and sound choices move into it (`sr2port/launcher`, `-DSR2_LAUNCHER=AUTO|ON|OFF`, wxWidgets 3.2); `sr2port` without the new `adlib`/`speaker` switches keeps the original text menus.
(Settled answers from U7 go here, dated.)

## TODO (parked)
- [ ] EGA, CGA, Tandy, Hercules graphics
