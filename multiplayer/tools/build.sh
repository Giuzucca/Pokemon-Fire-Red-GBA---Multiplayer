#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SOURCE=${POKEFIRERED_DIR:-"$ROOT/vendor/pokefirered"}
PATCH="$ROOT/patches/pokemon-fire-red-multiplayer.patch"

if [ ! -d "$SOURCE/.git" ]; then
    echo "Fonte pret/pokefirered não encontrada: $SOURCE" >&2
    echo "Consulte README.md para clonar a revisão necessária e fornecer baserom.gba." >&2
    exit 1
fi
if [ ! -f "$SOURCE/src/multiplayer.c" ]; then
    git -C "$SOURCE" apply --check "$PATCH"
    git -C "$SOURCE" apply "$PATCH"
fi
if [ ! -f "$SOURCE/baserom.gba" ]; then
    echo "Coloque sua cópia legal compatível em $SOURCE/baserom.gba" >&2
    exit 1
fi
make -C "$SOURCE" -j4 all syms
python3 "$ROOT/multiplayer/tools/package.py" --source "$SOURCE"
lua5.4 "$ROOT/multiplayer/tests/protocol_test.lua"
if { command -v mgba >/dev/null 2>&1; } || { command -v pkg-config >/dev/null 2>&1 && pkg-config --exists mgba; }; then
    MGBA_FLAGS=$(pkg-config --cflags --libs mgba 2>/dev/null || echo -lmgba)
    # shellcheck disable=SC2086
    cc -Wall -Wextra "$ROOT/multiplayer/tests/dual_core.c" -o "$ROOT/multiplayer/tests/dual_core" $MGBA_FLAGS
    "$ROOT/multiplayer/tests/dual_core" "$ROOT/dist/FireRed-MP.gba" "$ROOT/dist/host.lua" "$ROOT/dist/client.lua" "$SOURCE/pokefirered.sym"
else
    echo "libmgba não instalada; teste dual-core ignorado. ROM e teste Lua foram gerados/executados."
fi
