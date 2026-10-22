#!/usr/bin/env bash
# Compila Grounded 2 · Panel en español con MinGW-w64 (Linux, WSL o MSYS2) y genera el paquete.
#
#   ./build.sh            -> descarga/parchea el SDK si hace falta, compila, pasa las pruebas y empaqueta
#   ./build.sh --no-pack  -> solo compila
#
# Requisitos: x86_64-w64-mingw32-g++ (posix), cmake, python3, unzip, curl, zip.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD="$ROOT/build"
SDK_DIR="$BUILD/sdk"
SDK_URL="${G2_SDK_URL:-https://github.com/x0reaxeax/Grounded2Minimal/releases/download/sdk/Grounded2Minimal-SDK.zip}"
JOBS="$(nproc 2>/dev/null || echo 4)"
VERSION="$(sed -n 's/^project(Grounded2PanelES VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"

mkdir -p "$BUILD"

# 1) SDK de Dumper-7 publicado por Grounded2Minimal (no se redistribuye en este repositorio)
if [ ! -f "$SDK_DIR/.g2panel_patched" ]; then
    if [ ! -f "$BUILD/Grounded2Minimal-SDK.zip" ]; then
        echo "==> Descargando el SDK: $SDK_URL"
        curl -fL --retry 3 -o "$BUILD/Grounded2Minimal-SDK.zip" "$SDK_URL"
    fi
    echo "==> SHA-256 del SDK: $(sha256sum "$BUILD/Grounded2Minimal-SDK.zip" | cut -d' ' -f1)"
    rm -rf "$SDK_DIR" && mkdir -p "$SDK_DIR"
    unzip -q "$BUILD/Grounded2Minimal-SDK.zip" -d "$SDK_DIR"
    python3 -I "$ROOT/scripts/patch_sdk.py" "$SDK_DIR"
fi

# 2) Pruebas de la lógica independiente del juego (compilador nativo)
echo "==> Pruebas"
c++ -std=c++20 -O1 -Wall -Wextra -Werror -I"$ROOT/src" -I"$ROOT/src/panel" \
    "$ROOT/tests/logic_tests.cpp" "$ROOT/src/panel/game/Settings.cpp" -o "$BUILD/logic_tests"
"$BUILD/logic_tests"

# 3) Compilación cruzada
echo "==> Compilando (MinGW-w64, $JOBS hilos)"
cmake -S "$ROOT" -B "$BUILD/mingw" -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/mingw-w64.cmake" \
      -DCMAKE_BUILD_TYPE=Release -DG2_SDK_DIR="$SDK_DIR" -G "Unix Makefiles" > /dev/null
cmake --build "$BUILD/mingw" -j "$JOBS"

# 4) Las DLL/EXE solo pueden depender de bibliotecas de Windows
for bin in Grounded2PanelES.dll IniciarPanel.exe VistaPrevia.exe; do
    deps="$(x86_64-w64-mingw32-objdump -p "$BUILD/mingw/$bin" | sed -n 's/^\s*DLL Name: //p' | tr '\n' ' ')"
    echo "    $bin -> $deps"
    if echo "$deps" | grep -qiE 'libstdc|libgcc|libwinpthread'; then
        echo "ERROR: $bin depende de bibliotecas de MinGW" >&2
        exit 1
    fi
done

[ "${1:-}" = "--no-pack" ] && exit 0

# 5) Paquete para Vortex: la carpeta Augusta hace que Vortex lo instale en la raíz del juego
echo "==> Empaquetando"
PKG="$BUILD/paquete"
NAME="Grounded2PanelES-Steam-$VERSION"
DEST="$PKG/Augusta/Binaries/Win64/Grounded2PanelES"
rm -rf "$PKG" && mkdir -p "$DEST"
cp "$BUILD/mingw/Grounded2PanelES.dll" "$BUILD/mingw/IniciarPanel.exe" "$BUILD/mingw/VistaPrevia.exe" "$DEST/"
cp "$ROOT/LEEME.txt" "$DEST/LEEME.txt"
cp "$ROOT/LICENSE" "$DEST/LICENCIA-GPL-3.0.txt"
mkdir -p "$DEST/licencias"
cp "$ROOT/assets/fonts/"*.txt "$ROOT/third_party/imgui/LICENSE.txt" "$DEST/licencias/" 2>/dev/null || true
# Código fuente correspondiente (GPL-3.0)
git -C "$ROOT" archive --format=zip --prefix="Grounded2PanelES-$VERSION/" -o "$DEST/Codigo_fuente.zip" HEAD -- . 2>/dev/null \
    || (cd "$ROOT/.." && zip -qr "$DEST/Codigo_fuente.zip" "$(basename "$ROOT")" -x "*/build/*")
(cd "$DEST" && sha256sum Grounded2PanelES.dll IniciarPanel.exe VistaPrevia.exe > SHA256.txt)
mkdir -p "$ROOT/dist"
rm -f "$ROOT/dist/$NAME.zip"
(cd "$PKG" && zip -qr "$ROOT/dist/$NAME.zip" Augusta)
echo "==> Listo: dist/$NAME.zip"
