# Building the patch

The project builds on Windows as a 32-bit ASI. The build script and this guide
live in `src/`; the compiler, dependencies, configuration template, and output
paths are resolved relative to the repository root. The game executable is
not needed to build the patch.

## Requirements

- Windows 10 or Windows 11
- PowerShell 5.1 or newer
- LLVM-MinGW 20261006, using its `i686-w64-mingw32-clang.exe` target

MinHook v1.3.4 is included in `vendor/minhook-1.3.4` and is compiled into the ASI.

## Set up the compiler

Download the official
[`llvm-mingw-20261006-ucrt-x86_64.zip`](https://github.com/mstorsjo/llvm-mingw/releases/download/20261006/llvm-mingw-20261006-ucrt-x86_64.zip)
archive. Its SHA-256 is:

```text
317492c456aa27ee607a5919f1d2d38dcdc1112516a24d0bf4b00d078f52d17a
```

Verify the archive with `Get-FileHash`, then extract it into `toolchain/` at
the repository root. The default compiler path is:

```text
toolchain\llvm-mingw-20261006-ucrt-x86_64\bin\i686-w64-mingw32-clang.exe
```

The archive name describes the compiler host; the `i686` executable produces
the 32-bit game plugin. The downloaded toolchain is ignored by Git.

## Build

From the repository root:

```powershell
.\src\build.ps1
```

From inside `src/`:

```powershell
.\build.ps1
```

To use a compiler installed elsewhere:

```powershell
.\src\build.ps1 -Compiler "C:\Path\To\i686-w64-mingw32-clang.exe"
```

An absolute script path also works from a different working directory. If
PowerShell blocks a downloaded script, review it and run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\src\build.ps1
```

The script compiles the ASI, checks that the result is a PE32 x86 DLL, copies
the distribution files, and prints the binary's SHA-256. Compiler warnings
are errors; the bundled dependency's unused-parameter warning is suppressed.
This repository does not contain or run a test suite. A successful build and
architecture check do not substitute for in-game testing.

## Output

```text
dist/
  PacManWorld3FPSFix.asi
  PacManWorld3FPSFix.ini
  README.md
  TECHNICAL.md
  CHANGELOG.md
  LICENSE
  MinHook-LICENSE.txt
```

Only the ASI and INI are needed beside the game at runtime. Keep both license
files in any distributed archive. `dist/` is ignored by Git.

The script does not read a reference game executable and no longer accepts
`-ReferenceExe`. Executable verification is performed by the ASI when it is
loaded into the game; see [TECHNICAL.md](../TECHNICAL.md).

## GitHub Actions

[The build workflow](../.github/workflows/build.yml) downloads the same pinned
compiler, verifies the archive hash, runs `src/build.ps1`, and uploads a ZIP
containing the distribution files. It runs for pushes, pull requests, and
manual dispatches. It does not publish a GitHub release automatically.

See [RELEASING.md](../RELEASING.md) for publishing instructions.

## Troubleshooting

- **Compiler not found:** Check the default path above or pass `-Compiler`.
- **Bundled files missing:** Restore `vendor/minhook-1.3.4` and the root-level
  INI and documentation from the repository.
- **Compilation failed:** Read the compiler's first error. Ensure you selected
  the `i686` compiler rather than an x86-64 or ARM target.
- **Build succeeds but the game stays capped:** Consult the ASI's `.log` file
  and the diagnostic section of [TECHNICAL.md](../TECHNICAL.md).
