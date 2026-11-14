# Street Rod 2 Enhanced — agent rules (keep this file short: it is loaded every session)

Enhanced version of the faithful C/SDL3 port in `../street-rod-2` (cloned 2026-11-14, git remote `faithful`).
Goal: a **3D race view** (real 3D road from the track data, the original pictures as sprites), smooth at the display
rate, then rain and other effects. The garage, menus, rules and sound stay the faithful port's. State: `PLAN.md`
(the finished port's plan and history: `PORT_PLAN.md`, search it, do not read it whole).

Rules:
- Work on ONE subtask per session (PLAN.md id, e.g. `R1.2`). Read only PLAN.md "Status" + that subtask. Update its
  status line (last table cell: `done YYYY-MM-DD`), notes in the "Notes:" list under the table, commit.
  When you need something from the user (visual check, decision), add a row to the `## User to-do` table.
- **Game logic stays faithful.** Physics, opponents, timing and rules run exactly as in the faithful port; the 3D view
  only reads game state (plus interpolation between physics steps). New behaviour goes behind a switch with the
  original as fallback (`classic` switch = the faithful 2D race view).
- Bugs in shared code are fixed in `../street-rod-2` first, then pulled here (`git pull faithful main`).
- Specs are searched, never dumped: `port/spec/race_render.md` (renderer, track arrays §1.4), `race.md`, `FORMATS.md`.
- Tools write to `work/`; print short summaries. Do not view images unless asked; the user checks visuals.
- Windows PowerShell 5.1 corrupts UTF-8 files: edit with the Edit/Write tools or Python only.
- Build: MSYS2 UCRT64 (`C:\msys64\ucrt64\bin` on PATH), `cmake -S sr2port -B sr2port/build -G Ninja
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc`. Game data in `Game/` (git-ignored, never committed).
