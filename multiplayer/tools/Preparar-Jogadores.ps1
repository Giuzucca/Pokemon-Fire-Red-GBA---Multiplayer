param(
    [string]$IPHost = '127.0.0.1',
    [ValidateRange(1024,65535)][int]$Porta = 4096,
    [switch]$DetectHamachi
)
$ErrorActionPreference = 'Stop'
if ($DetectHamachi) {
    $candidates = @(Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object { $_.IPAddress -match '^25\.' } |
        Select-Object -ExpandProperty IPAddress -Unique)
    if ($candidates.Count -ne 1) {
        throw 'Nao foi possivel escolher um unico IPv4 Hamachi (25.x). Use -IPHost 25.x.x.x manualmente.'
    }
    $IPHost = $candidates[0]
}
$parsedIP = [System.Net.IPAddress]::Parse($IPHost)
if ($parsedIP.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork -or $parsedIP.ToString() -eq '0.0.0.0') {
    throw 'Use o IPv4 do computador anfitriao (Hamachi), ou 127.0.0.1 para teste nesta maquina.'
}
$IPHost = $parsedIP.ToString()
$rom = Join-Path $PSScriptRoot 'FireRed-MP.gba'
$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'manifest.json') -Raw | ConvertFrom-Json
if ((Get-FileHash -LiteralPath $rom -Algorithm SHA256).Hash.ToLowerInvariant() -ne $manifest.sha256) {
    throw 'ROM diferente do manifesto. Gere novamente o pacote completo.'
}
foreach ($role in @('host','client')) {
    $destination = Join-Path $PSScriptRoot $role
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    Copy-Item -LiteralPath $rom -Destination (Join-Path $destination "FireRed-MP-$role.gba") -Force
    $script = Get-Content -LiteralPath (Join-Path $PSScriptRoot "$role.lua") -Raw
    $script = $script -replace 'ip="[0-9.]+"', ('ip="' + $IPHost + '"')
    $script = $script -replace 'port=\d+', ('port=' + $Porta)
    [System.IO.File]::WriteAllText((Join-Path $destination 'Multiplayer.lua'), $script, [System.Text.UTF8Encoding]::new($false))
}
Write-Host "Pronto. Host: $IPHost`:$Porta. Pastas host/client com ROMs separadas; saves existentes preservados."
if ($DetectHamachi) { Write-Host 'IPv4 Hamachi detectado automaticamente.' }
Write-Host 'Abra a ROM correspondente no mGBA e carregue Multiplayer.lua em Tools > Scripting.'
