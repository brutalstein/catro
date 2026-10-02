param(
    # Also delete the local identity, servers, and settings under LocalAppData.
    [switch] $RemoveUserData
)

$ErrorActionPreference = 'Stop'

$installRoot = if ([string]::IsNullOrWhiteSpace($env:CATRO_INSTALL_ROOT)) {
    Join-Path (Join-Path $env:LOCALAPPDATA 'Programs') 'Catro'
} else {
    [IO.Path]::GetFullPath($env:CATRO_INSTALL_ROOT)
}

if (Get-Process -Name 'Catro' -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -and $_.Path.StartsWith($installRoot, [StringComparison]::OrdinalIgnoreCase) }) {
    throw 'Close Catro before uninstalling.'
}

if (Test-Path -LiteralPath $installRoot) {
    Remove-Item -LiteralPath $installRoot -Recurse -Force
    Write-Host "[catro] Removed $installRoot"
}

$shortcut = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Catro.lnk'
if (Test-Path -LiteralPath $shortcut) {
    Remove-Item -LiteralPath $shortcut -Force
}

$userData = Join-Path $env:LOCALAPPDATA 'Catro'
if ($RemoveUserData -and (Test-Path -LiteralPath $userData)) {
    Remove-Item -LiteralPath $userData -Recurse -Force
    Write-Host "[catro] Removed user data $userData"
} elseif (Test-Path -LiteralPath $userData) {
    Write-Host "[catro] Kept user data $userData (use -RemoveUserData to delete it)"
}
