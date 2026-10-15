param(
    [string]$ROMBase,
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
if (-not (Test-Path -LiteralPath $rom)) {
    if (-not $ROMBase) {
        throw 'Informe -ROMBase com sua ROM original FireRed USA 1.0 para gerar a ROM multiplayer.'
    }
    if (-not (Test-Path -LiteralPath $ROMBase -PathType Leaf)) {
        throw "ROM base nao encontrada em '$ROMBase'. Informe o caminho completo de um arquivo .gba existente."
    }
    $baseHash = (Get-FileHash -LiteralPath $ROMBase -Algorithm SHA1).Hash.ToLowerInvariant()
    if ($baseHash -ne '41cb23d8dccc8ebd7c649cd8fbb58eeace6e2fdc') {
        throw 'ROM base incompativel. Necessaria FireRed USA revisao 1.0 (SHA-1 41cb23d8dccc8ebd7c649cd8fbb58eeace6e2fdc).'
    }
    $bytes = [System.IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $ROMBase))
    $patch = [System.IO.File]::ReadAllBytes((Join-Path $PSScriptRoot 'FireRed-MP-v9.ips'))
    if ([System.Text.Encoding]::ASCII.GetString($patch, 0, 5) -ne 'PATCH') { throw 'Patch IPS invalido.' }
    $pos = 5
    while ($pos + 3 -le $patch.Length) {
        if ([System.Text.Encoding]::ASCII.GetString($patch, $pos, 3) -eq 'EOF') {
            $pos += 3
            break
        }
        if ($pos + 5 -gt $patch.Length) { throw 'Patch IPS incompleto.' }
        $offset = ([int]$patch[$pos] -shl 16) -bor ([int]$patch[$pos + 1] -shl 8) -bor [int]$patch[$pos + 2]
        $length = ([int]$patch[$pos + 3] -shl 8) -bor [int]$patch[$pos + 4]
        $pos += 5
        if ($length -eq 0 -or $pos + $length -gt $patch.Length -or $offset + $length -gt $bytes.Length) {
            throw 'Patch IPS corrompido ou fora dos limites da ROM.'
        }
        [Array]::Copy($patch, $pos, $bytes, $offset, $length)
        $pos += $length
    }
    if ($pos -ne $patch.Length) { throw 'Patch IPS sem terminador valido.' }
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { $builtHash = [BitConverter]::ToString($sha.ComputeHash($bytes)).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
    if ($builtHash -ne $manifest.sha256) { throw 'A ROM gerada nao corresponde a v9 esperada.' }
    [System.IO.File]::WriteAllBytes($rom, $bytes)
}
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
