# Paper Mario ReCut

Paper Mario ReCut is a native PC recompilation project for Paper Mario on Nintendo 64.

This repository does not include ROM files, extracted ROM assets, save files, or generated ROM-derived recomp output. The app prompts for a legally dumped Paper Mario (U) ROM on first run and installs a local validated copy into that user's own `user` folder.

## Runtime Notes

- Press F1 in-game to show or hide the early Windows menu bar for File, Graphics, and Controls.
- Press F10 in-game to show or hide the VI/FPS counter.

## Legal ROM Requirement

You must provide your own legally dumped Paper Mario (U) ROM. Do not commit or distribute ROMs, save files, generated ROM output, or local `user` folders.

The `.gitignore` intentionally blocks common N64 ROM/save extensions and local runtime folders.

## Building

This source tree expects generated Paper Mario recomp output at:

```text
generated/paper_mario_recomp_out/
```

That folder is intentionally not committed. Generate it locally from your own legal ROM before configuring CMake.

Then build with CMake and Visual Studio 2022:

```powershell
cmake -S . -B build-recut -G "Visual Studio 17 2022" -A x64
cmake --build build-recut --config Release
```
