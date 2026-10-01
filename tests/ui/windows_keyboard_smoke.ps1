param(
    [string]$Executable,
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $Executable) {
    $Executable = Join-Path $repoRoot 'out\apps\windows\x64\Debug\Catro.exe'
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repoRoot 'out\ui-smoke'
}

$Executable = (Resolve-Path $Executable).Path
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace CatroKeyboardSmoke {
    public static class Native {
        private const uint InputKeyboard = 1;
        private const uint KeyUp = 0x0002;
        private const ushort Shift = 0x10;

        [StructLayout(LayoutKind.Sequential)]
        public struct Point {
            public int X;
            public int Y;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct MouseInput {
            public int Dx;
            public int Dy;
            public uint MouseData;
            public uint Flags;
            public uint Time;
            public UIntPtr ExtraInfo;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct KeyboardInput {
            public ushort VirtualKey;
            public ushort ScanCode;
            public uint Flags;
            public uint Time;
            public UIntPtr ExtraInfo;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct HardwareInput {
            public uint Message;
            public ushort ParamLow;
            public ushort ParamHigh;
        }

        [StructLayout(LayoutKind.Explicit)]
        public struct InputUnion {
            [FieldOffset(0)] public MouseInput Mouse;
            [FieldOffset(0)] public KeyboardInput Keyboard;
            [FieldOffset(0)] public HardwareInput Hardware;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct Input {
            public uint Type;
            public InputUnion Data;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct Rect {
            public int Left;
            public int Top;
            public int Right;
            public int Bottom;
        }

        [DllImport("user32.dll", SetLastError = true)]
        private static extern uint SendInput(
            uint inputCount,
            Input[] inputs,
            int inputSize);

        [DllImport("user32.dll")]
        public static extern bool SetForegroundWindow(IntPtr window);

        [DllImport("user32.dll")]
        public static extern bool ShowWindow(IntPtr window, int command);

        [DllImport("user32.dll")]
        public static extern bool GetWindowRect(IntPtr window, out Rect rect);

        private static Input Key(ushort virtualKey, bool up) {
            return new Input {
                Type = InputKeyboard,
                Data = new InputUnion {
                    Keyboard = new KeyboardInput {
                        VirtualKey = virtualKey,
                        Flags = up ? KeyUp : 0
                    }
                }
            };
        }

        public static void SendKey(ushort virtualKey, bool shift) {
            var inputs = new List<Input>();
            if (shift) {
                inputs.Add(Key(Shift, false));
            }
            inputs.Add(Key(virtualKey, false));
            inputs.Add(Key(virtualKey, true));
            if (shift) {
                inputs.Add(Key(Shift, true));
            }

            var sent = SendInput(
                (uint)inputs.Count,
                inputs.ToArray(),
                Marshal.SizeOf(typeof(Input)));
            if (sent != inputs.Count) {
                throw new InvalidOperationException(
                    "SendInput failed with Win32 error " +
                    Marshal.GetLastWin32Error());
            }
        }
    }
}
'@

function Assert-CatroAlive {
    $script:process.Refresh()
    if ($script:process.HasExited) {
        throw "Catro exited unexpectedly with code $($script:process.ExitCode)."
    }
}

function Send-CatroKey {
    param(
        [Parameter(Mandatory)]
        [ushort]$VirtualKey,
        [switch]$Shift
    )

    Assert-CatroAlive
    [CatroKeyboardSmoke.Native]::SendKey($VirtualKey, $Shift.IsPresent)
    Start-Sleep -Milliseconds 350
    Assert-CatroAlive
}

function Save-CatroScreenshot {
    param([Parameter(Mandatory)][string]$Name)

    Assert-CatroAlive
    $rect = [CatroKeyboardSmoke.Native+Rect]::new()
    if (-not [CatroKeyboardSmoke.Native]::GetWindowRect(
            $script:process.MainWindowHandle,
            [ref]$rect)) {
        throw "GetWindowRect failed for Catro."
    }

    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -le 0 -or $height -le 0) {
        throw "Catro returned an invalid window rectangle."
    }

    $bitmap = [System.Drawing.Bitmap]::new($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen(
            $rect.Left,
            $rect.Top,
            0,
            0,
            $bitmap.Size)
        $path = Join-Path $OutputDirectory "$Name.png"
        $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

$startedHere = $false
try {
    $process = Start-Process -FilePath $Executable -PassThru
    $startedHere = $true

    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        Start-Sleep -Milliseconds 100
        Assert-CatroAlive
        $process.Refresh()
    } while (
        $process.MainWindowHandle -eq [IntPtr]::Zero -and
        [DateTime]::UtcNow -lt $deadline)

    if ($process.MainWindowHandle -eq [IntPtr]::Zero) {
        throw "Catro did not create a main window within 20 seconds."
    }

    [CatroKeyboardSmoke.Native]::ShowWindow(
        $process.MainWindowHandle,
        9) | Out-Null
    if (-not [CatroKeyboardSmoke.Native]::SetForegroundWindow(
            $process.MainWindowHandle)) {
        throw "Catro main window could not be focused for keyboard input."
    }
    Start-Sleep -Milliseconds 500

    Save-CatroScreenshot '00-launch'

    # Natural XAML tab order: personal server, add server, system, settings.
    Send-CatroKey 0x09
    Send-CatroKey 0x0D
    Save-CatroScreenshot '01-server'

    Send-CatroKey 0x09
    Send-CatroKey 0x09
    Send-CatroKey 0x0D
    Save-CatroScreenshot '02-system'

    Send-CatroKey 0x09
    Send-CatroKey 0x0D
    Save-CatroScreenshot '03-settings'

    Send-CatroKey 0x09 -Shift
    Send-CatroKey 0x09 -Shift
    Send-CatroKey 0x09 -Shift
    Send-CatroKey 0x0D
    Save-CatroScreenshot '04-server-return'

    # Walk into the server page and exercise a channel/action focus path.
    1..6 | ForEach-Object { Send-CatroKey 0x09 }
    Send-CatroKey 0x0D
    Save-CatroScreenshot '05-server-focus-path'

    Assert-CatroAlive
    Write-Host "[catro] Keyboard liveness screenshots captured; manual visual inspection required: $OutputDirectory"
}
finally {
    if ($startedHere -and $process -and -not $process.HasExited) {
        $process.CloseMainWindow() | Out-Null
        if (-not $process.WaitForExit(3000)) {
            $process.Kill()
        }
    }
}
