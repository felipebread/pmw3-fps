# Pac-Man has never seen this many frames!

Removes the FPS cap from the PC version of Pac-Man World 3. The target FPS
is configurable through an INI file, with 144 FPS selected by default. This patch targets the
RELOADED NoDisc PAL version of the game. The supported executable's SHA-256 is:

```text
4dda14f6f4269b55e755c8debf252ee6703167408959abc264bbed1fbb4114a9
```

## Features

- Allow you to raise the original 60 FPS limit.
- Lets you choose the target frame rate through a simple INI file.
- Tested alongside the FOV Fix and controller-support patches.
- Leaves the game files unchanged and is easy to remove.

## Requirements

- A compatible *Pac-Man World 3* executable using `PMA_W32_M.exe`.
- A compatible **32-bit ASI loader**.

The ASI loader is not included. ThirteenAG's Ultimate ASI Loader was used in
the tested configuration described below.
Source and build instructions are inside the src folder.

## Installation

1. Close the game.
2. Install your 32-bit ASI loader in the game folder.
3. Copy these two files into the folder scanned by your ASI loader, normally
   the same folder as `PMA_W32_M.exe`:

   - `PacManWorld3FPS.asi`
   - `PacManWorld3FPS.ini`

4. Open `PacManWorld3FPS.ini` in a text editor and choose your target FPS.
5. Save the file and start the game normally.

## Configuration

The included INI uses the following settings:

```ini
[FPS]
Enabled=1
TargetFPS=144
```

- `Enabled=1` enables the fix. Use `Enabled=0` to disable it.
- `TargetFPS=X` selects the desired frame rate.
- Values from 1 to 1000 are accepted.
- `TargetFPS=60` restores the game's original target.

Restart the game after changing the INI. Keep the INI beside the ASI. *If you*
*rename the ASI, give the INI the same base name.*

Your actual frame rate may still be limited by your PC, graphics settings,
display configuration, driver settings, or another frame limiter.

## How does this work?

Refer to TECHNICAL.md.

## Known issues

Some animations and camera movements run faster above 60 FPS. General gameplay
and player movement worked correctly during testing, but a complete playthrough
at every possible frame rate has not been verified.

## Uninstallation

Close the game and remove:

- `PacManWorld3FPS.asi`
- `PacManWorld3FPS.ini`
- `PacManWorld3FPS.log`, if present

## Compatibility tested

Version 1.0.0 was tested at 144 FPS with:

- ThirteenAG's Ultimate ASI Loader (32-bit version as `VERSION.dll`).
- Controller support provided through icup's `dinput8.dll` patches.
- Multiple widescreen patches.

## Source and development

### Disclaimer

AI contributed to the development of this project.

To build the ASI, follow [src/BUILD.md](src/BUILD.md). The build script is
`src/build.ps1`.

See [TECHNICAL.md](TECHNICAL.md) for the full timing analysis and implementation
details.

## Changelog

### 1.0.0 — 2026-10-09

- Added an INI to configure the patch
- Preserved the game frame timing and delta calculations
- Added strict executable and in-memory routine verification
- Added relative configuration and diagnostic logging

## License

This project is available under the MIT License. See `LICENSE` for details.
MinHook is distributed under its own BSD license; its license is included with
the project.
