# Third-Party Notices

## Vazirmatn

IrAutoX Launcher embeds **Vazirmatn Regular** for Persian/Arabic UI text during the Windows build.

- Project: Vazirmatn
- Upstream: https://github.com/rastikerdar/vazirmatn
- Font file used by the embedding script: `fonts/ttf/Vazirmatn-Regular.ttf`
- Copyright: Copyright 2015 The Vazirmatn Project Authors
- License: SIL Open Font License, Version 1.1 (OFL-1.1)
- Upstream license text: https://github.com/rastikerdar/vazirmatn/blob/master/OFL.txt

The font is downloaded from the official upstream repository by `scripts/embed-vazirmatn.ps1`, converted to Base64, embedded in the Qt resource system, and loaded from memory by the launcher. The font remains covered by OFL-1.1.

## IrAutoX UI icons

The SVG navigation and window-control icons under `resources/icons/` were created specifically for this repository as part of the IrAutoX Launcher UI refresh. They do not contain third-party icon packs or emoji assets.

## Game artwork

Game icons and banners shown in Store/Library are supplied by the IrAutoX backend for each published game. Publishers/administrators are responsible for only uploading artwork they have the right to distribute. IrAutoX Launcher does not bundle third-party game artwork in the application binary.
