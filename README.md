# Paper Mario ReCut

Paper Mario ReCut is a native PC recompilation project for Paper Mario on Nintendo 64.

This repository does not include ROM files, extracted ROM assets, save files, or generated ROM-derived recomp output. The app prompts for a legally dumped Paper Mario (U) ROM on first run and installs a local validated copy into that user's own `user` folder.

## Runtime Notes

- Press F1 in-game to show or hide the early Windows menu bar for File, Graphics, and Controls.
- Press F8 in-game to open Texture Replacement. Live Texture Replacement loads edited PNG/DDS files from `user/textures/replacements/` and hot-reloads them while enabled.

Paper Mario ReCut includes a small built-in texture replacement pack compiled into the Windows executable. Those hashes are always loaded and are restored from the exe at startup, even when Live Texture Replacement is disabled.

Texture replacement folders are local runtime data under `user/textures/`. Original dumps belong under `user/textures/dumps/`, replacement packs belong under `user/textures/replacements/`, and only the replacement folder is loaded by the live replacement toggle. The Dump Textures button performs a short current-scene dump pass while game input is held, then writes ordered PNG v5 files named like `000001_0123456789abcdef.v5.png` directly under `user/textures/dumps/`. Hash-named or ordered PNG/DDS files copied into `user/textures/replacements/` can be hot-loaded without hand-editing `rt64.json` and will override the built-in pack when Live Texture Replacement is on. Keep ROM files, saves, generated recomp output, local dumps, local replacement experiments, and the `user` folder out of commits unless a future legal texture pack is intentionally authored from scratch.

Paper Atlas Tool lives in editable source form at `tools/PaperAtlasTool/`. Windows builds publish `PaperAtlasTool.exe` beside `PaperMarioReCut.exe`; the Texture Replacement window and Graphics menu can launch it as a sidecar editor pointed at the dump and replacement folders.

## Legal ROM Requirement

You must provide your own legally dumped Paper Mario (U) ROM. Do not commit or distribute ROMs, save files, generated ROM output, or local `user` folders.

The `.gitignore` intentionally blocks common N64 ROM/save extensions and local runtime folders.

## Building

This source tree expects generated Paper Mario recomp output at:

```text
generated/paper_mario_recomp_out/
```

That folder is intentionally not committed. Generate it locally from your own legal ROM before configuring CMake.

Then build with CMake, Visual Studio 2022, and the .NET SDK:

```powershell
cmake -S . -B build-recut -G "Visual Studio 17 2022" -A x64
cmake --build build-recut --config Release
```
