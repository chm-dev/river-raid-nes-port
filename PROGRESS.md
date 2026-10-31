# PROGRESS — GBC port history (append-only)

Append-only log, one `### date - title` entry per session. Do not read it whole; grep it for a keyword.
The NES history is in `archive/nes/PROGRESS.md`.

### 2026-10-31 - Start GBC branch and reorganise docs (branch `gbc-port`)

- Created `gbc-port` from `experiment/gb-feasibility` and merged `main` (which added `GBC_PORT_PLAN.md`). `main` is unchanged.
- Moved the NES-only docs to `archive/nes/` with `git mv` (history kept): HANDOFF, PORTING_PLAN, PROGRESS, BUGS&FEATS, AGENTS, README, MESEN_MCP_NOTES, PERFORMANCE_SUMMARY, LESSONS.
- New `AGENTS.md`, `HANDOFF.md`, `PROGRESS.md`, `BUGS&FEATS.md`, `README.md` and a trimmed `info/LESSONS.md` (Atari RE section and the lessons that carry over).
- `GBC_PORT_PLAN.md` moved from `docs/` to the root. Gearboy MCP notes added to `AGENTS.md`.
- Code is unchanged: no NES code was moved or deleted. No build was run (there is no GBC build yet).
