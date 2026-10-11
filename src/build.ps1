param(
    [string]$Compiler = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$source = $PSScriptRoot
$project = Split-Path -Parent $source
$vendor = Join-Path $project 'vendor/minhook-1.3.4'
$output = Join-Path $project 'dist'
if ([string]::IsNullOrWhiteSpace($Compiler)) {
    $Compiler = Join-Path $project 'toolchain/llvm-mingw-20261006-ucrt-x86_64/bin/i686-w64-mingw32-clang.exe'
}
if (!(Test-Path -LiteralPath $Compiler -PathType Leaf)) {
    throw '32-bit LLVM-MinGW compiler not found. See src/BUILD.md or supply -Compiler.'
}
$Compiler = (Resolve-Path -LiteralPath $Compiler).Path
$packageFiles = @('PacManWorld3FPSFix.ini', 'README.md', 'TECHNICAL.md', 'LICENSE', 'CHANGELOG.md')
foreach ($file in $packageFiles) {
    if (!(Test-Path -LiteralPath (Join-Path $project $file) -PathType Leaf)) {
        throw "Required distribution file missing: $file"
    }
}
if (!(Test-Path -LiteralPath "$vendor/include/MinHook.h") -or
    !(Test-Path -LiteralPath "$vendor/LICENSE.txt")) {
    throw 'Bundled MinHook files are missing. Restore vendor/minhook-1.3.4.'
}
New-Item -ItemType Directory -Force -Path $output | Out-Null
$common = @('-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-D_WIN32_WINNT=0x0601',
    '-I',"$vendor/include",'-I',"$source")
$sources = @("$source/hook.S", "$vendor/src/buffer.c", "$vendor/src/hook.c",
    "$vendor/src/trampoline.c", "$vendor/src/hde/hde32.c")
$asi = Join-Path $output 'PacManWorld3FPSFix.asi'
& $Compiler @common '-shared' "$source/patch.c" @sources '-lbcrypt' '-static-libgcc' `
    '-Wl,--exclude-all-symbols,--no-insert-timestamp' '-o' "$output/PacManWorld3FPSFix.asi"
if ($LASTEXITCODE -ne 0) { throw 'ASI compilation failed.' }

# Verify that the compiler produced a PE32 x86 DLL, irrespective of its extension.
$image = [IO.File]::ReadAllBytes($asi)
if ($image.Length -lt 64 -or [BitConverter]::ToUInt16($image, 0) -ne 0x5A4D) {
    throw 'Build output is not a Windows PE image.'
}
$pe = [BitConverter]::ToInt32($image, 0x3C)
if ($pe -lt 64 -or $pe -gt $image.Length - 26 -or
    [BitConverter]::ToUInt32($image, $pe) -ne 0x4550 -or
    [BitConverter]::ToUInt16($image, $pe + 4) -ne 0x014C -or
    [BitConverter]::ToUInt16($image, $pe + 24) -ne 0x010B -or
    ([BitConverter]::ToUInt16($image, $pe + 22) -band 0x2000) -eq 0) {
    throw 'Build output must be a 32-bit x86 DLL.'
}
foreach ($file in $packageFiles) {
    Copy-Item -LiteralPath (Join-Path $project $file) -Destination $output
}
Copy-Item -LiteralPath "$vendor/LICENSE.txt" -Destination "$output/MinHook-LICENSE.txt"
Write-Host "Built and verified x86 ASI: $asi"
Get-FileHash -LiteralPath $asi -Algorithm SHA256
