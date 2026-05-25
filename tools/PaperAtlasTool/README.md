# Paper Atlas Tool

Editable C# WinForms sidecar app for arranging dumped Paper Mario texture PNG pieces into an atlas, editing them in an external image editor, and splitting the edited atlas back into replacement PNGs.

## Project Integration

The source lives here so the sidecar can be changed with the rest of Paper Mario ReCut. The main Windows CMake build publishes a single-file `PaperAtlasTool.exe` beside `PaperMarioReCut.exe`.

The game launches the tool with:

```text
--source <user/textures/dumps> --replacements <user/textures/replacements>
```

You can also run it directly from this folder:

```powershell
dotnet run --project N64TextureAtlasEditor.csproj -- --source "C:\Paper Mario ReCut\user\textures\dumps" --replacements "C:\Paper Mario ReCut\user\textures\replacements"
```

## Manual Publish

```powershell
dotnet publish N64TextureAtlasEditor.csproj --configuration Release --runtime win-x64 --self-contained true -p:PublishSingleFile=true -p:EnableCompressionInSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -p:DebugSymbols=false -p:DebugType=none -p:PublishReadyToRun=false --output publish
```

## Features

- Load dumped PNG folders.
- Pan and zoom a live atlas canvas.
- Drag, marquee-select, multi-select, nudge, lock, remove, and restore pieces.
- Auto-pack and edge-alignment helpers.
- Choose an external image editor.
- Edit selected pieces as a combined layout.
- Split edited layouts back into the replacements folder.
- Save `combined_texture.png` and `n64_texture_layout.json`.
