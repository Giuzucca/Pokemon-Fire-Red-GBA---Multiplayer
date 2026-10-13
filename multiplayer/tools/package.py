"""Package a compiled ROM plus standalone scripts for mGBA. Never touches old saves."""
from pathlib import Path
import argparse
import hashlib
import ipaddress
import json
import re
import shutil
import zlib

ROOT = Path(__file__).resolve().parents[2]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--ip', default='127.0.0.1', help='Host interface / Hamachi IPv4')
    parser.add_argument('--source', type=Path, default=ROOT/'vendor/pokefirered', help='patched pret/pokefirered checkout')
    parser.add_argument('--port', type=int, default=4096)
    parser.add_argument('--output', type=Path, default=ROOT/'dist')
    args = parser.parse_args()
    ip = str(ipaddress.IPv4Address(args.ip))
    if ip == '0.0.0.0' or not 1024 <= args.port <= 65535:
        parser.error('Use a specific host IPv4 and a port between 1024 and 65535')
    source = args.source.resolve()
    rom = source/'pokefirered.gba'
    blob = rom.read_bytes()
    symbols = (source/'pokefirered.sym').read_text()
    match = re.search(r'^([0-9a-fA-F]+)\s+.*\bgMultiplayer$', symbols, re.M)
    if not match:
        raise SystemExit('gMultiplayer not found; compile the modified ROM first')
    address = int(match[1],16)
    if not 0x02000000 <= address <= 0x02040000-84:
        raise SystemExit('Mailbox outside EWRAM')
    def symbol(name):
        found = re.search(r'^([0-9a-fA-F]+)\s+.*\b'+re.escape(name)+r'$',symbols,re.M)
        if not found: raise SystemExit('Missing symbol: '+name)
        return int(found[1],16)
    manifest = dict(version=3,stage='duel-wild-trainer-rematch-coop',sha256=hashlib.sha256(blob).hexdigest(),
                    crc32=f'{zlib.crc32(blob):08x}',mailbox=address,port=args.port,ip=ip)
    manifest.update(sendBuffer=symbol('gBlockSendBuffer'),recvBuffer=symbol('gBlockRecvBuffer'))
    args.output.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(rom,args.output/'FireRed-MP.gba')
    (args.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    protocol = (ROOT/'multiplayer/src/protocol.lua').read_text()
    bridge = (ROOT/'multiplayer/src/bridge.lua').read_text()
    for role in ('host','client'):
        config = (f'-- Generated for FireRed-MP. Change only ip/port for your private network.\n'
                  f'local CONFIG = {{role="{role}", ip="{ip}", port={args.port}, '
                  f'mailbox=0x{address:08x}, sendBuffer=0x{manifest["sendBuffer"]:08x}, recvBuffer=0x{manifest["recvBuffer"]:08x}, '
                  f'build="{manifest["sha256"]}", crc32="{manifest["crc32"]}"}}\n')
        (args.output/f'{role}.lua').write_text(config+'local Protocol = (function()\n'+protocol+'\nend)()\n'+bridge,encoding='utf-8')
    for name in ('Preparar-Jogadores.ps1','Iniciar.ps1'):
        shutil.copyfile(ROOT/'multiplayer/tools'/name,args.output/name)
    shutil.copyfile(ROOT/'README.md',args.output/'LEIA-ME.md')
    print(json.dumps(manifest,indent=2))

if __name__ == '__main__':
    main()
