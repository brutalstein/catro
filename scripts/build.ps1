# Configures and builds the core, tools, tests, and the WinUI shell. Writes only under out/ and
# apps/windows/packages (NuGet restore of the pinned shell packages).
param(
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Debug',
    [switch] $SkipShell
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root 'out\build\windows-msvc'

# Materialize the generated old-school mascot as a normal native Windows icon before MSBuild.
# Source control keeps the bytes as base64 text because the GitHub automation path is text-safe.
$assetDir = Join-Path $root 'apps\windows\Catro\Assets'
$iconBase64 = Join-Path $assetDir 'Catro.ico.b64'
$iconPath = Join-Path $assetDir 'Catro.ico'
if (Test-Path $iconBase64) {
    New-Item -ItemType Directory -Force -Path $assetDir | Out-Null
    [IO.File]::WriteAllBytes(
        $iconPath,
        [Convert]::FromBase64String((Get-Content -Raw $iconBase64).Trim()))

    # Sparse identity requires PNG visual assets. Derive them from the same icon so taskbar,
    # package identity, and in-app branding stay visually consistent.
    Add-Type -AssemblyName System.Drawing
    $sourceIcon = New-Object System.Drawing.Icon($iconPath)
    $sourceBitmap = $sourceIcon.ToBitmap()
    foreach ($size in @(44, 150)) {
        $bitmap = New-Object System.Drawing.Bitmap($size, $size)
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.Clear([System.Drawing.Color]::Transparent)
            $graphics.InterpolationMode =
                [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.DrawImage($sourceBitmap, 0, 0, $size, $size)
            $bitmap.Save(
                (Join-Path $assetDir "Catro$size.png"),
                [System.Drawing.Imaging.ImageFormat]::Png)
        } finally {
            $graphics.Dispose()
            $bitmap.Dispose()
        }
    }
    $sourceBitmap.Dispose()
    $sourceIcon.Dispose()
}

function Resolve-CatroVisualStudioGenerator {
    $help = (cmake --help 2>&1 | Out-String)
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        throw 'vswhere.exe not found; run scripts/bootstrap.ps1.'
    }

    $vsJson = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json
    if ($LASTEXITCODE -ne 0 -or -not $vsJson) {
        throw 'No Visual Studio installation with MSVC x64 tools was found; run scripts/bootstrap.ps1.'
    }
    $vs = ($vsJson | ConvertFrom-Json | Select-Object -First 1)
    $major = ([version]$vs.installationVersion).Major

    $candidates = @()
    if ($major -ge 18) { $candidates += 'Visual Studio 18 2026' }
    if ($major -ge 17) { $candidates += 'Visual Studio 17 2022' }

    foreach ($candidate in $candidates) {
        if ($help.Contains($candidate)) {
            return $candidate
        }
    }

    throw "CMake does not expose a generator compatible with $($vs.displayName) $($vs.installationVersion). Run 'cmake --help' and inspect the available generators."
}

Push-Location $root
try {
    $generator = Resolve-CatroVisualStudioGenerator
    Write-Host "[catro] Configure generator: $generator"
    Write-Host "[catro] Build directory: $buildDir"

    $cachePath = Join-Path $buildDir 'CMakeCache.txt'
    if (Test-Path $cachePath) {
        $existingGenerator = Get-Content $cachePath |
            Where-Object { $_ -like 'CMAKE_GENERATOR:INTERNAL=*' } |
            Select-Object -First 1
        if ($existingGenerator) {
            $existingGenerator = $existingGenerator.Substring('CMAKE_GENERATOR:INTERNAL='.Length)
            if ($existingGenerator -ne $generator) {
                throw "Build directory was configured with '$existingGenerator' but '$generator' is active now. Remove '$buildDir' and rerun build.ps1."
            }
        }
    }

    cmake -S $root -B $buildDir -G $generator -A x64 -DCATRO_BUILD_TESTS=ON
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

    cmake --build $buildDir --config $Configuration --parallel
    if ($LASTEXITCODE -ne 0) { throw 'CMake build failed.' }
    if ($SkipShell) { return }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $msbuild = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'MSBuild\**\Bin\MSBuild.exe' |
        Select-Object -First 1
    if (-not $msbuild) { throw 'MSBuild not found; run scripts/bootstrap.ps1.' }

    Write-Host "[catro] Building WinUI shell with MSBuild: $msbuild"
    & $msbuild (Join-Path $root 'apps\windows\Catro.sln') -restore -p:RestorePackagesConfig=true `
        "-p:Configuration=$Configuration" -p:Platform=x64 "-p:CatroCoreRoot=$buildDir" -m -nologo -v:m
    if ($LASTEXITCODE -ne 0) { throw 'WinUI shell build failed.' }

    Write-Host "[catro] Shell: out\apps\windows\x64\$Configuration\Catro.exe"
} finally {
    Pop-Location
}
