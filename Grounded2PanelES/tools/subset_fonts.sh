#!/usr/bin/env bash
# Regenera los subconjuntos de fuentes de assets/fonts (requiere: pip install fonttools).
# Uso: tools/subset_fonts.sh <Nunito_600SemiBold.ttf> <Nunito_800ExtraBold.ttf> <Fredoka_600SemiBold.ttf> <fa-solid-900.ttf>
set -euo pipefail
OUT="$(cd "$(dirname "$0")/.." && pwd)/assets/fonts"
TEXT="U+0020-007E,U+00A0-00FF,U+0152-0153,U+2013-2014,U+2018-201E,U+2022,U+2026,U+2190-2193,U+2212,U+00D7"
for f in "$1" "$2" "$3"; do
    pyftsubset "$f" --unicodes="$TEXT" --layout-features='kern,liga' --output-file="$OUT/$(basename "${f%.ttf}")-subset.ttf"
done
# Iconos usados por src/common/Theme.hpp (namespace Icon)
ICONS=$(grep -o '// [ef][0-9a-f]\{3\}' "$OUT/../../src/common/Theme.hpp" | awk '{printf "U+%s,", $2}')
pyftsubset "$4" --unicodes="${ICONS%,}" --no-hinting --output-file="$OUT/fa-solid-subset.ttf"
