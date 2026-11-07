# Comix Zone — a native port by static recompilation

A native port of the Windows 95 version of *Comix Zone* to Linux, Windows and Android. The
original `COMIX.EXE` is statically recompiled with [SR](https://github.com/M-HT/SR): every
instruction of the game becomes native code, and the game runs its own logic as it did in
Windows 95. What Windows, WinG and the Miles Sound System gave it comes from the port's
runtime, [SR-SDK](SR-SDK/README.md), on SDL2.

No game data is included. The build and the game need your own copy of the game's CD.

## The original

| | |
|---|---|
| Game | *Comix Zone*, a side-scrolling beat 'em up set inside the pages of a comic book |
| Developer, publisher | Sega (Sega Technical Institute made the Mega Drive original, 1995) |
| Year | 1996, the Windows 95 version |
| Platform | Windows 95, CD-ROM |
| Executable | `COMIX.EXE`, a PE32 GUI program: Microsoft Visual C++ |
| Graphics | WinG (`WING32.DLL`): a 256-colour picture, blitted to a GDI window |
| Sound | the Miles Sound System (`WAIL32.DLL`): digital samples |
| Music | XMIDI sequences, played by Miles on the MIDI device |
| Input | keyboard (`GetAsyncKeyState`), joystick; the window menu with its dialogs |

## How the port works

- **Recompilation.** SR's Win32 recompiler (SRW) translates `COMIX.EXE` into llasm, an
  intermediate language. SR-SDK lowers it to LLVM IR, and LLVM compiles it for x86-64 and
  arm64. The game's data stays at the addresses it had in Windows 95
  (`guest_data_in_memory=yes` in `game/game.cfg`).
- **Win32 runtime** (SR-SDK `runtime/win32`): the windows and messages, GDI, the file
  system, the registry, the menu bar of the window and the game's dialogs (Setup
  Keyboard, Setup Gamepad, About), drawn over the picture.
- **WinG** (`game/WinApi-wing.c`): the game's device context and its palette-indexed
  bitmap are the port's own; the picture goes to an SDL window, through OpenGL when a
  filter is chosen.
- **Miles** (`game/WinApi-ail.c`): the sample handles play through SDL_mixer. The XMIDI
  music is turned into MIDI and played by SR-SDK's synthesizer (TinySoundFont) with a
  General MIDI bank, `game/install/soundfont.sf2` (a trimmed FluidR3_GM).

## What works

- The game plays: the menus, the levels, sound and music, the settings, saved in
  `COMIX.INI` as the original does.
- Linux (x86-64, arm64), Windows (x86-64; it also runs under Wine) and Android (arm64),
  with on-screen buttons and a touch on the top strip for the window menu.
- The window menu (Full Screen or Window, the settings dialogs), and Esc switching the
  mode as in the original.
- The port's panel (F12): the size of the picture, the window or the full screen, the
  frame rate, the gamepad, and the port's settings, saved at once in `comix-zone.cfg`.
- Gamepads, through SDL's game controller support.

## Mods

By default the port looks and plays as the original. A mod is a key of `comix-zone.cfg`,
off by default, switched in the panel too.

| Key | What it does |
|---|---|
| `Display_Shader` | a filter of the picture, through OpenGL: `smooth` (sharp bilinear), `pixelart` (Scale2x), `scanlines`, `crt` |

## Building

Requirements: Python 3, SCons, GCC or Clang, LLVM's `opt` and `llc`, SDL2 and SDL2_mixer;
MinGW-w64 for Windows; the Android SDK and NDK for Android (see
[SR-SDK's README](SR-SDK/README.md#requirements)).

The recompiler reads `COMIX.EXE` from the CD, so the image is needed for the build as well.
Put it where `image=` of [`game/portable.cfg`](game/portable.cfg) points, beside the
repository: `../iso/Comix.bin` (with its `Comix.cue`). The build checks the executable
against the SHA-256 in `game/recomp/`.

```sh
git clone --recursive <this repository>
cd comix_zone-recomp
make linux      # build/bin/pc64-linux/comix-zone
make windows    # MinGW-w64 cross build, with the SDL DLLs
make apk        # Android, arm64
make distrib    # the release archives, in build/distrib/
```

## Running

```sh
./run-sdl.sh            # the Linux build
./run-sdl-wine.sh       # the Windows build under Wine
./run-orig-wine.sh      # the original COMIX.EXE under Wine, for comparison
```

The launchers run the game on `build/distrib/data`: the game directory as the game's
installer makes it, made from the image by `make distrib-data`. The port writes into
`build/run`.

A release archive (`make distrib`) holds the program and that game directory; its
`readme-*.txt` and `comix-zone.cfg` describe the settings and the environment variables.

## License

The code of this repository is under the MIT license, as its file headers say. SR-SDK
and SR are under the MIT license as well: see `SR-SDK/LICENSE` and `SR-SDK/NOTICE.md`.
SR is by Roman Pauer; this port stands on it. The sound bank `game/install/soundfont.sf2`
is FluidR3_GM by Frank Wen, under the MIT license (`game/install/soundfont.LICENSE`).

*Comix Zone* itself, its executable and its data belong to their owners and are not part
of this repository.
