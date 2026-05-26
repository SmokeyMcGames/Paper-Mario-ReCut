# Paper Mario ReCut

![Paper Mario ReCut title logo](assets/title_logo.png)
### <img width="1285" height="994" alt="Pic1" src="https://github.com/user-attachments/assets/47b4af91-d8b3-4905-a525-ad5b05453eb8" />
Paper Mario ReCut is a native Windows PC recompilation project for *Paper Mario* on Nintendo 64. It is built around the N64 recompilation toolchain, RT64 rendering, local legal ROM setup, live texture replacement, and the bundled Paper Atlas Tool for editing dumped texture pieces.

This repository does not include ROM files, extracted ROM assets, save files, or generated ROM-derived recomp output. On first run, the app asks for your legally dumped Paper Mario (U) ROM, validates it, and installs a local copy into that user's own `user` folder.

## Features

- PRESS F1 TO ACCESS THE MENU (Might change to ESC not sure yet lol)
- First-run legal ROM setup with local validation.
- Windowed RT64 renderer integration.
- Graphics options menu with live-applying renderer settings.
- Live Texture Replacement toggle via F2.
- One-shot texture dumping with an in-window percentage and dump count.
- Continuous Dump mode for capturing newly created textures while the game keeps running.
- Paper Atlas Tool sidecar built for easy texture replacement and editing.
- Controller and keyboard configuration windows are present and still being expanded.

NOTE: The current Gamepad Implementaion will auto bind controls to known SDL controllers and the rebind system is currently in the works.

## Current Status

This is still an early working build. The game boots and the tooling is actively being shaped around Paper Mario as development continues.

Save states are implemented as an early runtime snapshot system. Slot saves and loads are queued onto Paper Mario's main game-loop boundary and store slots in `user/states/`. Treat them as testable while the runtime continues to mature. 

### Known issues:
1. Widescreen is currently broken, but it is still exposed for testing. Expect visual problems if you enable it. The normal 4:3 path is the intended play path for now.
2. Using Save States in it's current implementation will break the game. Avoid For Now.
3. Smartscreen is false positive until the app becomes signed. I have even submitted the exe for evaluation from microsoft with the response being just give it time for trust to be built. 
I apologize for any fear of the situation.

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
<img width="1296" height="1058" alt="image" src="https://github.com/user-attachments/assets/fb50c23f-0cfe-470d-a20a-65d06c213a23" />

Paper Atlas Tool is included as a means for simple texture replacement and as it evolves will change the way Paper Mario will be experienced making texture modding very simple.
I originally was working on this tool for texture replacement for any N64 texture set but have repurposed it just for this and still has a lot of work to be done.

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
