# Technical explanation

This document describes the patch implementation and
the reverse engineering that led to it. The patch raises the engine's target
frame rate while retaining its original frame pacing and timestep updates.
It is an x86 Windows DLL distributed with an `.asi` extension and loaded by a
separate ASI loader.

The implementation is in `src/patch.c`, `src/hook.S`, `src/hook.h`, and
`src/config.h`. Build instructions are in [src/BUILD.md](src/BUILD.md). Source
paths in this document refer to the repository layout.

## Supported executable

| Property | Required value |
| --- | --- |
| Tested filename | `PMA_W32_M.exe` |
| SHA-256 | `4dda14f6f4269b55e755c8debf252ee6703167408959abc264bbed1fbb4114a9` |
| File size | 15,388,672 bytes |
| Architecture | PE32, x86 / `IMAGE_FILE_MACHINE_I386` |
| Runtime image base | `0x00400000` |
| PE `SizeOfImage` | `0x0161E000` |
| Timing function RVA | `0x0026ECC0` |
| Timing function virtual address | `0x0066ECC0` |
| Timing function file offset | `0x0026E0C0` |
| Verified routine length | `0x178` bytes (376 bytes) |

An RVA is relative to the loaded executable's base. A file offset refers to
bytes in the executable on disk; it cannot be substituted for a runtime
address. The implementation deliberately requires the fixed image base above:
the verified routine contains absolute-address operands, so relocation would
invalidate its direct byte comparison.

The embedded engine string is `Babel 120.0.230, Mon May 8 11:14:29 2006` (the
binary contains an extra space before the single-digit day). It identifies
an embedded engine build, not proof of the executable's distribution source.

## How the original frame limit works

The main loop calls the rendering/presentation routine at `0x00679A93`, then
loads the application object and calls the timing function at `0x00679AA1`.
The application pointer is stored at `0x0087CA10`, or module RVA `0x47CA10`.
The timing function receives that object in ECX.

The following field names are inferred from the disassembly, not recovered
source-code symbols:

| Object offset | Type | Observed purpose |
| --- | --- | --- |
| `+0x08`, `+0x0C` | 64-bit timestamp | Previous frame timestamp |
| `+0x10`, `+0x14` | 64-bit timestamp | Current frame timestamp |
| `+0x20` | 32-bit float | Target FPS |
| `+0x24` | 32-bit integer | Enables the frame-pacing wait when nonzero |
| `+0x28` | 32-bit float | Conditional time multiplier |
| `+0x2C` | 32-bit integer | Controls application of the time multiplier |
| `+0x30` | 32-bit integer | Selects a target-derived timestep when nonzero |

The function reads the engine clock, calculates the elapsed time, and derives
an observed frame rate. If the wait is enabled and the observed rate is above
the target, it calls `Sleep(0)`, reads the clock again, and repeats. In effect,
it waits until enough time has passed for the desired frame interval.

Afterward, it calculates the timestep and updates the engine's timing globals.
One path substitutes the target rate when the fixed-step selector is set or
the measured rate falls below the routine's low-rate threshold (2 FPS).

The following is simplified pseudocode. It omits bookkeeping, additional
branches, and the conditional time multiplier:

```cpp
previous = current;
current = readEngineClock();
rate = rateFromElapsed(current - previous);

while (limitEnabled && rate > targetFPS) {
    Sleep(0);
    current = readEngineClock();
    rate = rateFromElapsed(current - previous);
}

if (fixedStepFlag || rate < 2.0f)
    rate = targetFPS;

deltaSeconds = 1.0f / rate;
normalizedDelta = deltaSeconds * 60.0f;
```

The routine initially writes seconds per update to `0x0087CA58` and
`0x0087CA34`. The conditional time multiplier can subsequently adjust
`0x0087CA58`. Multiplying the respective values by 60 produces the normalized
values at `0x0087CA44` and `0x0087CA54`. It also advances accumulated timing
values at `0x0087CA60` and `0x0087CA48`.

At an observed rate of 144 FPS without an additional time multiplier, the
expected values are approximately:

```text
delta seconds       = 1 / 144      = 0.00694444
60-Hz-normalized     = 60 / 144     = 0.41666667
```

These were observed during the original live experiment, together with an
increase to 144 FPS. This connected the target field to both frame pacing and
the game's existing timestep calculation.

## Why the target is reapplied on every timing call

The default integer at `0x0078E0D8` is 60. The configuration path converts it
to float and stores it at object offset `+0x20` at `0x006688D0`. An additional
instruction at `0x00F898BA` explicitly stores `0x42700000`, the IEEE-754
representation of `60.0f`, into the same target field.

The exact trigger for that second assignment was not established. Reapplying
the configured target immediately before the timing routine uses it makes a
one-time initialization write unnecessary and handles later target resets.

The hook uses the ECX object supplied by the game on each call. It does not
cache a heap address or depend on a pointer remaining unchanged across a
level transition or game restart.

## Detour and original-function trampoline

MinHook v1.3.4 installs a detour at the timing function's entry. For this
prologue, the whole instructions copied into the trampoline span seven bytes:

```asm
0066ECC0  51          push ecx
0066ECC1  56          push esi
0066ECC2  8B F1       mov  esi,ecx
0066ECC4  8B 46 10    mov  eax,[esi+10h]
```

MinHook supplies `g_original`, a pointer to a trampoline that executes those
instructions and transfers control to the remaining original function. Its
implementation handles executable memory, page protection, instruction-cache
flushing, and thread coordination during patch installation.

`src/hook.S` implements this minimal detour, shown in Intel-style notation:

```asm
push eax
mov  eax,[g_targetBits]
mov  [ecx+20h],eax
pop  eax
mov  dword ptr [g_hookSeen],1
jmp  dword ptr [g_original]
```

`g_targetBits` holds the configured float's raw 32-bit representation. The
initializer copies the float into that integer with `memcpy`, and the hook
copies those bits into the game's target field.

Only EAX is used as temporary storage, and it is restored. The instructions
do not alter arithmetic flags or use x87/SSE registers. Stack depth is restored
before the tail jump, so the game's original caller and return address are
preserved. The original timing function still runs normally and supplies its
original return behavior.

The only game-data write performed by this hook is `[ECX+0x20]`. It also sets
the plugin's own `g_hookSeen` flag so initialization can report that a frame
call was observed. It does not directly write the global delta values, change
the wait-enable flag, or replace the game's timer.

## Initialization and lifetime

`DllMain` records the ASI module handle, disables thread notifications, and
creates an initialization worker. It does not wait for that worker or install
hooks while executing the loader callback.

The worker performs these steps:

1. Derive `.ini` and `.log` paths from the loaded ASI's full path.
2. Open the log and read the configuration.
3. Validate the running executable's on-disk identity and timing bytes.
4. Validate the loaded image and compare its timing routine with those bytes.
5. Pin the ASI in memory for the lifetime of the game process.
6. Initialize MinHook, create the detour, and enable it.
7. Check every 100 ms for up to 30 seconds whether the hook has executed.

Pinning prevents the installed jump from pointing into an unloaded ASI.
The implementation has no live unload or configuration-reload mechanism.
To disable it, set `Enabled=0` and restart, or close the game and remove the
ASI. Process exit releases the installed in-memory patch along with the game.

If no timing call is observed during the initial 30 seconds, the hook remains
installed. That diagnostic can also mean the game is still starting; the
initializer does not interpret it as proof that the hook is broken.

## Executable and memory verification

`VerifyDisk` opens the running executable for reading, checks its exact file
size, and computes SHA-256 through Windows BCrypt. It reads the expected
timing-routine bytes from the same file handle after verifying the digest.
It never changes the executable on disk.

`VerifyMemory` then checks the base address, DOS/PE signatures, x86 machine
type, PE32 optional-header type, and expected image size. It reads all 376
bytes of the timing routine from the current process and compares them to
the verified file bytes. A mismatch prevents installation.

This is intentionally narrower than a pattern scan. It rejects different
executables, on-disk modifications, relocation, and existing modifications
inside that timing routine. In-memory changes elsewhere may coexist with the
patch. Passing these checks does not guarantee compatibility with every other
mod: a later patch can still overwrite the hook or change related behavior.

## Configuration and logging

```ini
[FPS]
Enabled=1
TargetFPS=144
```

The default values are enabled and 144 FPS. `Enabled` must be `0` or `1` as
returned by the Windows INI reader. Invalid settings cause initialization to
stop without installing the hook. A missing INI uses the defaults and produces
a diagnostic message.

`ParseTarget` uses `wcstod`, rejects conversion failures, range errors,
non-finite numbers, trailing non-whitespace characters, and values outside
1 through 1000 inclusive. Fractional values such as `143.9` are accepted; use
a dot for the decimal separator. The final stored target is a 32-bit float.

The INI is read once during initialization. Its basename must match the ASI:
renaming `PacManWorld3FPSFix.asi` to `Example.asi` changes the expected paths to
`Example.ini` and `Example.log` in that ASI's directory.

The log is recreated on each initialization and is closed after the worker
finishes. Messages are also sent through `OutputDebugString`, including when
the plugin cannot create a log file. The frame hook itself does not perform
file I/O or produce a log entry every frame.

| Message or symptom | Interpretation |
| --- | --- |
| `Disabled by INI.` | Configuration intentionally prevented hook installation. |
| `Invalid TargetFPS` | Correct the target's syntax or range and restart. |
| `Executable verification failed.` | On-disk size/hash or file reading did not meet verification requirements. |
| `Runtime timing routine does not match` | Image requirements failed or timing bytes were already modified. |
| `Hook installation failed` | MinHook reported a failure; its status text follows in the log. |
| `Frame hook executed` | The configured target was written during a timing call; this does not measure the achieved FPS. |
| No log | Check whether the loader loaded the ASI and whether its directory permits writing. |

## Earlier leads and Direct3D

An earlier candidate at `0x0060AEC6` passed 16 ms to a Sleep wrapper in a
callback loop. Changing that argument did not affect the observed frame rate.
That loop was not established as the main presentation limiter.

The engine clock has an RDTSC path and QueryPerformanceCounter/GetTickCount
fallbacks. Therefore, missing a particular Windows timer breakpoint does not
exclude engine-side timing. Driver and overlay calls to timer APIs also need
to be distinguished from calls made by the game.

The main D3D9 Present call is at `0x0061AC72`. The presentation parameters used
in the analyzed setup include `PresentationInterval` at `0x0083CCD8`, or module
RVA `0x43CCD8`. During the successful 144 FPS experiment, that value remained
`1`. The patch does not hook Direct3D or alter this field. That observation is
specific to the tested setup and does not establish how every presentation
mode or driver will limit FPS.

The original engine pacing loop still uses `Sleep(0)`. This patch introduces
no high-resolution replacement scheduler or guarantee of perfectly even frame
times. Actual performance can be limited by rendering cost, the driver,
display settings, or another limiter.

## Observed behavior and limits of verification

The original experiment raised FPS to 144 and produced approximately
`0.00694` delta seconds and `0.416` normalized delta. I report that general gameplay and movement remain intact;
some camera movements and animations become faster above 60 FPS and are minor
aesthetic differences in the observed gameplay.

The patch does not attempt to repair every animation or camera update. A
successful result at 144 FPS does not certify every setting in the accepted
1–1000 range or every scene in the game.

Earlier development included synthetic hook/register-preservation and config/
loader checks. Those test sources are not part of this publication layout.
The current build performs compilation and a PE architecture check only; it
does not launch the game or rerun the earlier test suite. Future behavior
changes should be validated with new in-game observations.

## Repository and distribution

`src/build.ps1` resolves repository resources from its own location, compiles
`patch.c`, `hook.S`, and MinHook's x86 sources, and writes the ASI to `dist/`.
Its linker options omit the PE timestamp and automatic symbol exports.
The build also copies the INI, user documentation, this technical explanation,
changelog, and both applicable licenses into that directory.

The project code is MIT-licensed. MinHook and its disassembler components
retain the notices in `vendor/minhook-1.3.4/LICENSE.txt`, copied into binary
packages as `MinHook-LICENSE.txt`. The game executable and the external ASI
loader are not part of the repository or its distribution package.

### Publication build verification — 2026-10-11

The relocated build script was run under Windows PowerShell 5.1 from both the
repository root and `src/`, using the pinned LLVM-MinGW compiler. Both builds
produced a PE32 x86 DLL with a zero PE timestamp and the following SHA-256:

```text
f9e50c38770d8fd5ba06e3bcd829b9dfe3608b9f1a03eecdb83e9d8a57fa1183
```

This matches the previously verified 1.0.0 ASI byte for byte. The four runtime
source files also match the supplied source folder. Import inspection showed
Windows system libraries, including BCrypt and the Universal CRT, with no
separate MinHook DLL dependency. The GitHub-hosted workflow has not been run
as part of this local preparation.
