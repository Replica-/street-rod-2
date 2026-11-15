# Street Rod 2 port — agent rules (keep this file short: it is loaded every session)

Faithful C/SDL3 port of California Dreams' *Street Rod 2: The Next Generation* (1991), a **delta
port** of the Street Rod engine already done in `../StreetRod` (RE → formats → specs → port).
State and subtasks: `PLAN.md`.

Token budget rules:
- Work on ONE subtask per session (PLAN.md id, e.g. `S1.2`). Read only `PLAN.md` "Status" + that
  subtask, not the whole file. Update its status line, run `python tools/plan_site.py`, commit.
  PLAN.md tables: status goes in the row's last cell (`done YYYY-MM-DD`, `U5 pending`); longer notes
  go in the "Notes:" list under the table, never between rows (that breaks the table).
  When you need something from the user (U-check, capture, dump, decision), add a row to the
  `## User to-do` table at the top of PLAN.md; remove rows that are done.
- Never read large files whole. `Grep`/`rg` first, then `Read` with offset/limit. Specs, decomps and
  disassembly are always searched, never dumped.
- Tools write to `work/` (or `port/`); print only a short summary (counts, first lines, errors).
- Do not view images or screenshots unless asked. Visual checks are the user's job (PLAN.md "U" tasks):
  write the PNG / contact sheet, then ask the user what they see.
- **SR1 first.** Before analysing an SR2 function, look it up in `port/sr2_sr1_matches.csv` (from
  S1.3). `same` → reuse the SR1 name, spec section (`../StreetRod/port/spec/`) and code
  (`../StreetRod/srport/src/`); spend effort only on `changed`/`new`. SR1 facts:
  `../StreetRod/port/RE_GUIDE.md`, `../StreetRod/FORMATS.md`. Search them, do not dump them.
- Reuse before writing: `../StreetRod/srport/` (host, mem, video, loader), `tools/` here,
  Nuked-OPL3 from `../F117-2/third_party/`. Copy files with the shell; do not read them into context
  unless they must change. Never edit `../StreetRod*` from here.
- Wide searches → the `scout` agent (Haiku). Do not re-derive facts already in `port/RE_GUIDE.md`,
  `FORMATS.md` or `port/spec/`.
- Ground truth when a spec is unclear: `python tools/x86dis.py work/SR2_unp.exe dis SSSS:OOOO LEN`.
- Windows PowerShell 5.1 corrupts UTF-8 files: edit with the Edit/Write tools or Python only.
- Game logic stays faithful. Enhancements (3D race view `src/r3d/`, effects) live in this repo, never a separate
  one, each behind a switch with the original as fallback (`--classic`). New ideas: a row in PLAN.md "Enhanced backlog".
- Game data is never committed (`Game/` is git-ignored).
- No test HTML pages; implement, then tell the user what to test.
- Picture touch-ups from the user: they mark them in the mask editor (launch.json "Mask editor": `tools/mask_server.py`
  serves `tools/mask_editor.html` at `http://localhost:8765/?img=<repo path>`) and press Send to Claude. Each submission
  is a folder `work/edits/<date-time>_<picture>/`: `picture.png`, `mask_<n>.png` (white = change, picture resolution),
  `marked.png`, `request.txt` (one line per touch-up). Run `bash tools/mask_watch.sh` in the background: it prints every
  submission not in `work/edits/done.txt` (all touch-ups); action each touch-up in order, append the folder to done.txt, rerun.
  F12 in the game saves the whole window (side strips included) to `work/shots/` and opens it in the editor.
  Only pixels we made go in the repo, under `sr2port/assets/gfx/`: new pictures whole (`wide/garage_l.bmp`), edits to the
  game's pictures as patches `<stem>@<x>,<y>.bmp` (alpha 0 = keep, e.g. `pic_1018@119,0.bmp`), applied over the
  generated gfx/ picture (enh/amiga.c). Never commit a whole game picture. Build copies them beside the exe; release ships them.
