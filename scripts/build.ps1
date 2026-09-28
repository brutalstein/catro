# Configures and builds the core, tools, tests, and the WinUI shell. Writes only under out/ and
# apps/windows/packages (NuGet restore of the pinned shell packages).
param(
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Debug',
    [switch] $SkipShell
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root 'out\build\windows-msvc'

# Generate the native old-school Catro mascot deterministically at build time.
# Keeping the source as drawing primitives avoids binary/base64 corruption in text-only automation,
# while RC.exe still receives a normal multi-resolution .ico file.
$assetDir = Join-Path $root 'apps\windows\Catro\Assets'
$iconPath = Join-Path $assetDir 'Catro.ico'

function New-CatroIconAssets {
    New-Item -ItemType Directory -Force -Path $assetDir | Out-Null
    Add-Type -AssemblyName System.Drawing

    $baseSize = 256
    $source = New-Object System.Drawing.Bitmap($baseSize, $baseSize)
    $graphics = [System.Drawing.Graphics]::FromImage($source)

    $dark = [System.Drawing.Color]::FromArgb(255, 52, 35, 28)
    $brown = [System.Drawing.Color]::FromArgb(255, 95, 58, 41)
    $orange = [System.Drawing.Color]::FromArgb(255, 194, 108, 63)
    $tan = [System.Drawing.Color]::FromArgb(255, 210, 157, 99)
    $cream = [System.Drawing.Color]::FromArgb(255, 232, 208, 164)
    $black = [System.Drawing.Color]::FromArgb(255, 25, 22, 20)
    $muted = [System.Drawing.Color]::FromArgb(255, 145, 113, 88)
    $gold = [System.Drawing.Color]::FromArgb(255, 235, 190, 120)

    $darkBrush = New-Object System.Drawing.SolidBrush($dark)
    $brownBrush = New-Object System.Drawing.SolidBrush($brown)
    $orangeBrush = New-Object System.Drawing.SolidBrush($orange)
    $tanBrush = New-Object System.Drawing.SolidBrush($tan)
    $creamBrush = New-Object System.Drawing.SolidBrush($cream)
    $blackBrush = New-Object System.Drawing.SolidBrush($black)
    $mutedBrush = New-Object System.Drawing.SolidBrush($muted)
    $goldPen = New-Object System.Drawing.Pen($gold, 8)
    $darkPen = New-Object System.Drawing.Pen($dark, 7)
    $blackPen = New-Object System.Drawing.Pen($black, 18)
    $mutedPen = New-Object System.Drawing.Pen($muted, 7)

    try {
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.InterpolationMode =
            [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.Clear([System.Drawing.Color]::Transparent)

        # Vintage badge + cat head.
        $graphics.FillEllipse($darkBrush, 18, 18, 220, 220)
        $graphics.DrawEllipse($goldPen, 18, 18, 220, 220)
        $graphics.FillEllipse($brownBrush, 30, 30, 196, 196)

        $leftEar = [System.Drawing.Point[]]@(
            [System.Drawing.Point]::new(70, 103),
            [System.Drawing.Point]::new(83, 43),
            [System.Drawing.Point]::new(116, 84))
        $rightEar = [System.Drawing.Point[]]@(
            [System.Drawing.Point]::new(140, 84),
            [System.Drawing.Point]::new(173, 43),
            [System.Drawing.Point]::new(186, 103))
        $graphics.FillPolygon($orangeBrush, $leftEar)
        $graphics.FillPolygon($orangeBrush, $rightEar)
        $graphics.DrawPolygon($darkPen, $leftEar)
        $graphics.DrawPolygon($darkPen, $rightEar)

        $graphics.FillEllipse($creamBrush, 60, 72, 136, 134)
        $graphics.DrawEllipse($darkPen, 60, 72, 136, 134)
        $graphics.FillEllipse($tanBrush, 94, 132, 68, 56)

        # Headphones: thick black outer band with muted-metal inset and warm ear cups.
        $graphics.DrawArc($blackPen, 45, 39, 166, 140, 190, 160)
        $graphics.DrawArc($mutedPen, 51, 45, 154, 128, 190, 160)
        $graphics.FillRectangle($blackBrush, 42, 105, 34, 64)
        $graphics.FillRectangle($blackBrush, 180, 105, 34, 64)
        $graphics.FillRectangle($orangeBrush, 48, 111, 22, 52)
        $graphics.FillRectangle($orangeBrush, 186, 111, 22, 52)

        # Sleepy old-school expression.
        $eyePen = New-Object System.Drawing.Pen($black, 6)
        $mouthPen = New-Object System.Drawing.Pen($dark, 4)
        try {
            $graphics.DrawArc($eyePen, 85, 108, 32, 26, 10, 160)
            $graphics.DrawArc($eyePen, 139, 108, 32, 26, 10, 160)
            $nose = [System.Drawing.Point[]]@(
                [System.Drawing.Point]::new(128, 145),
                [System.Drawing.Point]::new(119, 152),
                [System.Drawing.Point]::new(137, 152))
            $graphics.FillPolygon($darkBrush, $nose)
            $graphics.DrawArc($mouthPen, 106, 148, 23, 23, 300, 145)
            $graphics.DrawArc($mouthPen, 127, 148, 23, 23, 95, 145)
        } finally {
            $eyePen.Dispose()
            $mouthPen.Dispose()
        }

        # CATRO plaque remains legible at medium/large icon sizes.
        $plaqueBrush = New-Object System.Drawing.SolidBrush(
            [System.Drawing.Color]::FromArgb(235, 25, 22, 20))
        $plaquePen = New-Object System.Drawing.Pen($orange, 3)
        $font = New-Object System.Drawing.Font(
            [System.Drawing.FontFamily]::GenericSerif,
            18,
            [System.Drawing.FontStyle]::Bold,
            [System.Drawing.GraphicsUnit]::Pixel)
        $textBrush = New-Object System.Drawing.SolidBrush($cream)
        try {
            $graphics.FillRectangle($plaqueBrush, 76, 202, 104, 27)
            $graphics.DrawRectangle($plaquePen, 76, 202, 104, 27)
            $format = New-Object System.Drawing.StringFormat
            try {
                $format.Alignment = [System.Drawing.StringAlignment]::Center
                $format.LineAlignment = [System.Drawing.StringAlignment]::Center
                $graphics.DrawString(
                    'CATRO',
                    $font,
                    $textBrush,
                    [System.Drawing.RectangleF]::new(76, 202, 104, 27),
                    $format)
            } finally {
                $format.Dispose()
            }
        } finally {
            $plaqueBrush.Dispose()
            $plaquePen.Dispose()
            $font.Dispose()
            $textBrush.Dispose()
        }

        # Save sparse-identity logos from the same source drawing.
        foreach ($size in @(44, 150)) {
            $bitmap = New-Object System.Drawing.Bitmap($size, $size)
            $resize = [System.Drawing.Graphics]::FromImage($bitmap)
            try {
                $resize.Clear([System.Drawing.Color]::Transparent)
                $resize.SmoothingMode =
                    [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
                $resize.InterpolationMode =
                    [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $resize.DrawImage($source, 0, 0, $size, $size)
                $bitmap.Save(
                    (Join-Path $assetDir "Catro$size.png"),
                    [System.Drawing.Imaging.ImageFormat]::Png)
            } finally {
                $resize.Dispose()
                $bitmap.Dispose()
            }
        }

        # Build a real multi-resolution ICO. Windows Vista+ accepts PNG-compressed image entries,
        # so each size stays compact and retains alpha without a hand-written DIB mask.
        $entries = @()
        foreach ($size in @(16, 24, 32, 48, 64, 128, 256)) {
            $bitmap = New-Object System.Drawing.Bitmap($size, $size)
            $resize = [System.Drawing.Graphics]::FromImage($bitmap)
            $memory = New-Object IO.MemoryStream
            try {
                $resize.Clear([System.Drawing.Color]::Transparent)
                $resize.SmoothingMode =
                    [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
                $resize.InterpolationMode =
                    [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $resize.DrawImage($source, 0, 0, $size, $size)
                $bitmap.Save($memory, [System.Drawing.Imaging.ImageFormat]::Png)
                $entries += [pscustomobject]@{
                    Size = $size
                    Bytes = $memory.ToArray()
                }
            } finally {
                $memory.Dispose()
                $resize.Dispose()
                $bitmap.Dispose()
            }
        }

        $stream = [IO.File]::Open(
            $iconPath,
            [IO.FileMode]::Create,
            [IO.FileAccess]::Write,
            [IO.FileShare]::None)
        $writer = New-Object IO.BinaryWriter($stream)
        try {
            $writer.Write([UInt16]0)
            $writer.Write([UInt16]1)
            $writer.Write([UInt16]$entries.Count)

            [UInt32]$offset = 6 + (16 * $entries.Count)
            foreach ($entry in $entries) {
                $dimension = if ($entry.Size -ge 256) { 0 } else { $entry.Size }
                $writer.Write([byte]$dimension)
                $writer.Write([byte]$dimension)
                $writer.Write([byte]0)
                $writer.Write([byte]0)
                $writer.Write([UInt16]1)
                $writer.Write([UInt16]32)
                $writer.Write([UInt32]$entry.Bytes.Length)
                $writer.Write([UInt32]$offset)
                $offset += [UInt32]$entry.Bytes.Length
            }
            foreach ($entry in $entries) {
                $writer.Write([byte[]]$entry.Bytes)
            }
        } finally {
            $writer.Dispose()
            $stream.Dispose()
        }
    } finally {
        $goldPen.Dispose()
        $darkPen.Dispose()
        $blackPen.Dispose()
        $mutedPen.Dispose()
        $darkBrush.Dispose()
        $brownBrush.Dispose()
        $orangeBrush.Dispose()
        $tanBrush.Dispose()
        $creamBrush.Dispose()
        $blackBrush.Dispose()
        $mutedBrush.Dispose()
        $graphics.Dispose()
        $source.Dispose()
    }
}

New-CatroIconAssets

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
