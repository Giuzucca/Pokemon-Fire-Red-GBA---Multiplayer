param(
    [Parameter(Mandatory=$true)][ValidateSet('host','client')][string]$Papel,
    [string]$Emulador
)
$ErrorActionPreference = 'Stop'
if (-not $Emulador) {
    $candidate = Join-Path $PSScriptRoot '..\..\mGBA.exe'
    if (Test-Path -LiteralPath $candidate) { $Emulador = $candidate }
    else { $Emulador = Join-Path $PSScriptRoot 'mGBA.exe' }
}
$Emulador = (Resolve-Path -LiteralPath $Emulador).Path
$rom = Join-Path $PSScriptRoot "$Papel\FireRed-MP-$Papel.gba"
if (-not (Test-Path -LiteralPath $rom)) { throw 'Execute Preparar-Jogadores.ps1 primeiro.' }
# Explicitly launched interactive game window; background helpers are not used.
Start-Process -FilePath $Emulador -ArgumentList @(('"' + $rom + '"')) -WorkingDirectory $PSScriptRoot
Write-Host "No mGBA: Tools > Scripting > Load script, e escolha $Papel\Multiplayer.lua."
