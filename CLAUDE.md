# touch-ttd: project rules

This checkout is OpenTTD with a touch-first mobile layout being added on top. Upstream's own docs (`README.md`, `COMPILING.md`, `CODINGSTYLE.md`) still apply to all code.

## Documentation is part of every change (project rule)

Every piece of work on the mobile UI, Android/iOS builds or tooling is documented in `docs/mobile/` **as it is done**, not afterwards:

- **Steps:** what was done, in order, with the files and functions touched.
- **Findings:** non-obvious facts learned from the code (where something lives, what an API assumes, what broke and why).
- **Tips:** what would save the next person time: commands, gotchas, debugging tricks.

How:
- Add to the log of the current round (`docs/mobile/NN-<topic>-log.md`), or start a new numbered file for a new round or topic.
- Toolchain and setup changes go in `docs/mobile/00-toolchain.md`. Keep its commands copy-pasteable and verified.
- Keep `docs/mobile/README.md` (the index) up to date when adding a file.
- Design handoffs (to and from the design session) go in `docs/mobile/handoffs/`.
- Teaching notes for the user (what to learn next, which new code makes a good example) go in `docs/mobile/teaching-resume.md`.

A task is not finished until its notes are written.

## Code conventions for the mobile layout

- All shell windows live in `src/mobile_gui.cpp`; widget IDs in `src/widgets/mobile_widget.h`. The prefixes are `WID_MS_`, `WID_MD_`, `WID_MB_`, `WID_MM_`, `WID_MHUB_`, `WID_MN_` and `WID_MMB_`. Widget enum names are global across the script API, so check for collisions (`grep -rn "WID_XX_" src/widgets`).
- New `WindowClass` values go at the **end** of the enum (script API stability).
- Desktop behaviour must not change while `gui.mobile_layout` is off. Guard mobile paths with `IsMobileLayout()`.
- Game actions go through the command system (`Command<Commands::…>::Post`), never by changing game state directly, so multiplayer stays compatible.
