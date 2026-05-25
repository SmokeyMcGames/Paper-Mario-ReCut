# Paper Mario ReCut

![Paper Mario ReCut title logo](assets/title_logo.png)

Paper Mario ReCut is a native Windows PC recompilation project for *Paper Mario* on Nintendo 64. It is built around the N64 recompilation toolchain, RT64 rendering, local legal ROM setup, live texture replacement, and the bundled Paper Atlas Tool for editing dumped texture pieces.

This repository does not include ROM files, extracted ROM assets, save files, or generated ROM-derived recomp output. On first run, the app asks for your legally dumped Paper Mario (U) ROM, validates it, and installs a local copy into that user's own `user` folder.

## Features

- Native Windows executable for Paper Mario ReCut.
- First-run legal ROM setup with local validation.
- Windowed RT64 renderer integration.
- Windows menu bar toggled with F1.
- File menu with exit/restart and Save State / Load State submenus for slots 1-5.
- Graphics options menu with live-applying renderer settings.
- Texture Replacement window toggled with F8.
- Live Texture Replacement toggle, also available with F2.
- One-shot texture dumping with an in-window percentage and dump count.
- Continuous Dump mode for capturing newly created textures while the game keeps running.
- Texture dumping skips PNG v5 assets that are already present in the dump folder.
- Built-in ReCut texture pack embedded in the executable.
- Editable Paper Atlas Tool sidecar built from source in this repo.
- Paper Atlas auto-selects `user/textures/dumps` and `user/textures/replacements` when launched beside the game.
- Paper Atlas saves combined atlas work to `user/AtlasEditing` so dump folders stay clean.
- Paper Atlas can group pieces by similar resolution to make preview cleanup easier.
- Controller and keyboard configuration windows are present and still being expanded.

## Current Status

This is still an early working build. The game boots and the tooling is actively being shaped around Paper Mario rather than Zelda Recompiled.

Save states are implemented as an early runtime snapshot system. Slot saves and loads are queued onto Paper Mario's main game-loop boundary and store slots in `user/states/`. Treat them as testable while the runtime continues to mature. 
CAUTION: Using Save States in it's current implementation will break the game. Avoid For Now.

Known issue: widescreen is currently broken, but it is still exposed for testing. Expect visual problems if you enable it. The normal 4:3 path is the intended play path for now.

## Runtime Folders

Local runtime data lives under:

```text
user/
```

Important subfolders:

```text
user/pm.n64.us.z64
user/states/
user/textures/dumps/
user/textures/replacements/
user/AtlasEditing/
```

Do not commit or distribute ROMs, save files, generated ROM output, local dumps, local replacements, or `user` folders.

## Paper Atlas Tool

Paper Atlas Tool is included as editable C# WinForms source at:

```text
tools/PaperAtlasTool/
```

Windows builds publish `PaperAtlasTool.exe` beside `PaperMarioReCut.exe`. You can open it from Graphics > Paper Atlas Tool or from the Texture Replacement window. If the expected dump or replacement folders are missing, the game and Atlas tool explain how to create them.

## Building

This source tree expects generated Paper Mario recomp output at:

```text
generated/paper_mario_recomp_out/
```

That folder is intentionally not committed. Generate it locally from your own legal ROM before configuring CMake.

Requirements:

- Visual Studio 2022 with C++ desktop tools.
- CMake.
- .NET 8 SDK for Paper Atlas Tool.

Build:

```powershell
cmake -S . -B build-recut -G "Visual Studio 17 2022" -A x64
cmake --build build-recut --config Release
```

The Windows build output is created in:

```text
build-recut/Release/
```

## Texture Replacement Instructions

1. Start Paper Mario ReCut and load the scene whose textures you want to edit.
2. Press F8, or open Graphics > Texture Replacement.
3. Hover Dump Textures for a reminder of what it does.
4. Press Dump Textures. Game input pauses briefly while the current scene's loaded textures are written as PNG v5 files to:

```text
user/textures/dumps/
```

5. The progress area shows a percentage plus how many textures are available as it dumps. Existing PNG v5 files are skipped, so repeat dumps only add newly seen textures.
6. For a broader capture, enable Continuous Dump. It keeps dumping textures as the game creates them and can slow the game down heavily depending on hardware, so turn it off when you are done.
7. Open Paper Atlas Tool from the Texture Replacement window or Graphics > Paper Atlas Tool.
8. Paper Atlas auto-selects the dump and replacement folders when launched beside the game exe.
9. Arrange pieces manually, use Auto Pack, Auto Edges, or Group Sizes.
10. Save Atlas + Layout. The combined work files are saved to:

```text
user/AtlasEditing/
```

11. Edit `combined_texture.png` in your image editor.
12. Return to Paper Atlas and split the atlas back to replacements. Output pieces are written to:

```text
user/textures/replacements/
```

13. In the game, enable Live Texture Replacement or press F2. You can also use Reload Folder after editing files.
