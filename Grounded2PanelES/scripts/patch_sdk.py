#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Adapta un SDK generado por Dumper-7 (pensado para MSVC) para que compile con
MinGW-w64 (GCC) y para que los offsets globales se puedan resolver en tiempo
de ejecucion.

Cambios que aplica:
  1. Offsets::GObjects/AppendString/GNames/GWorld/ProcessEvent* pasan de
     `constexpr` a `inline` (asi el panel puede corregirlos al vuelo cuando el
     juego se actualiza).
  2. UWorld::GetWorld() deja de usar `if constexpr` sobre GWorld.
  3. Relleno explicito al final de cada clase: GCC (ABI Itanium) reutiliza el
     relleno final de las clases base, MSVC no. Sin esto los miembros de las
     clases derivadas quedarian desplazados. Los static_assert del propio SDK
     verifican el resultado al compilar.
  4. Valores de enum fuera del rango de su tipo base (MSVC los trunca, GCC da
     error) se truncan igual que MSVC.
  5. Pequenos arreglos de compatibilidad (std::sqrt sin <cmath>, sombreado de
     parametros de plantilla).

Uso: patch_sdk.py <directorio_del_sdk>
El directorio debe contener SDK.hpp, UnrealContainers.hpp, Assertions.inl y SDK/.
"""

import os
import re
import sys

MEMBER_RE = re.compile(r"//\s*0x([0-9A-Fa-f]+)\(0x([0-9A-Fa-f]+)\)")
CLASS_START_RE = re.compile(
    r"^(?:class|struct)\s+(?:alignas\((0x[0-9A-Fa-f]+)\)\s+)?(\w+)(?:\s+final)?(?:\s*:\s*public\s+[\w:<>, ]+)?\s*$"
)
ENUM_START_RE = re.compile(r"^enum class (\w+) : (\w+)\s*$")
ENUM_VALUE_RE = re.compile(r"^(\s*\w+\s*=\s*)(-?\d+)(,.*)$")
ARRAY_RE = re.compile(r"\[0x([0-9A-Fa-f]+)\]\s*;")
SIZEOF_RE = re.compile(r"static_assert\(sizeof\((\w+)\) == 0x([0-9A-Fa-f]+),")

TYPE_BITS = {
    "uint8": (8, False), "int8": (8, True),
    "uint16": (16, False), "int16": (16, True),
    "uint32": (32, False), "int32": (32, True),
    "uint64": (64, False), "int64": (64, True),
}


def read(path):
    with open(path, "r", encoding="utf-8-sig", errors="surrogateescape") as f:
        return f.read()


def write(path, text):
    with open(path, "w", encoding="utf-8", errors="surrogateescape", newline="\n") as f:
        f.write(text)


def clamp_enum_value(value, type_name):
    bits, signed = TYPE_BITS.get(type_name, (64, False))
    if bits == 64:
        return value
    mask = (1 << bits) - 1
    v = value & mask
    if signed and v >= (1 << (bits - 1)):
        v -= (1 << bits)
    return v


def fix_enums(lines):
    out = []
    enum_type = None
    changed = 0
    for line in lines:
        m = ENUM_START_RE.match(line)
        if m:
            enum_type = m.group(2)
        elif enum_type and line.startswith("};"):
            enum_type = None
        elif enum_type:
            mv = ENUM_VALUE_RE.match(line)
            if mv:
                value = int(mv.group(2))
                fixed = clamp_enum_value(value, enum_type)
                if fixed != value:
                    line = f"{mv.group(1)}{fixed}{mv.group(3)}"
                    changed += 1
        out.append(line)
    return out, changed


def fix_tail_padding(lines, sizes):
    """Inserta un relleno final explicito donde la clase termina antes de su sizeof()."""
    out = []
    i = 0
    n = len(lines)
    changed = 0
    while i < n:
        line = lines[i]
        m = CLASS_START_RE.match(line)
        if not m or i + 1 >= n or lines[i + 1].strip() != "{":
            out.append(line)
            i += 1
            continue

        explicit_align = int(m.group(1), 16) if m.group(1) else 0
        name = m.group(2)
        # Recoger el cuerpo de la clase hasta el '};' que la cierra.
        depth = 0
        body = []
        j = i + 1
        while j < n:
            body.append(lines[j])
            depth += lines[j].count("{") - lines[j].count("}")
            if depth == 0:
                break
            j += 1

        end = 0
        has_members = False
        for b in body:
            s = b.strip()
            if s.startswith("static ") or s.startswith("using ") or s.startswith("constexpr ") or s.startswith("//"):
                continue
            mm = MEMBER_RE.search(b)
            code = b.split("//")[0]
            if mm and ";" in code:
                off = int(mm.group(1), 16)
                size = int(mm.group(2), 16)
                # En arrays el comentario indica el tamano de UN elemento
                # (salvo los Pad_ de Dumper-7, que ya indican el total).
                arr = ARRAY_RE.search(code)
                if arr and not re.search(r"\bPad_\w*\[", code):
                    size *= int(arr.group(1), 16)
                end = max(end, off + size)
                has_members = True

        expected = sizes.get(name)
        # Con alignas(), MSVC coloca los miembros de la clase derivada justo
        # tras los datos de la base (dentro del relleno de alineacion), igual
        # que GCC. En ese caso no hay que tocar nada.
        if explicit_align and expected is not None and expected == ((end + explicit_align - 1) // explicit_align) * explicit_align:
            expected = None
        out.append(line)
        if has_members and expected is not None and expected > end and body and body[-1].strip().startswith("}"):
            pad = expected - end
            out.extend(body[:-1])
            out.append("public:")
            out.append(f"\tuint8 Pad_G2TailFix_{end:X}[0x{pad:X}]; // relleno MSVC (anadido por patch_sdk.py)")
            out.append(body[-1])
            changed += 1
        else:
            out.extend(body)
        i = j + 1
    return out, changed


TUOBJECTARRAY = """// TUObjectArray (Grounded2PanelES): the panel detects the layout at runtime.
//   flat:    { FUObjectItem* Objects; int32 MaxElements; int32 NumElements; }
//   chunked: { FUObjectItem** Chunks; FUObjectItem* PreAllocated; int32 MaxElements; int32 NumElements; ... }
class TUObjectArray final
{
public:
	static inline bool  Chunked = false;
	static inline int32 ItemSize = 0x18;
	static inline int32 ObjectOffset = 0x00;   // offset of the UObject* inside FUObjectItem
	static inline int32 NumOffset = 0x0C;
	static inline int32 ElementsPerChunk = 0x10000;

	uint8 Raw[0x10];

public:
	inline int32 Num() const
	{
		return *reinterpret_cast<const int32*>(reinterpret_cast<const uint8*>(this) + NumOffset);
	}

	inline class UObject* GetByIndex(const int32 Index) const
	{
		if (Index < 0 || Index >= Num())
			return nullptr;

		const uint8* Base = *reinterpret_cast<uint8* const*>(this);
		if (!Base)
			return nullptr;

		if (Chunked)
		{
			const uint8* Chunk = reinterpret_cast<uint8* const*>(Base)[Index / ElementsPerChunk];
			if (!Chunk)
				return nullptr;
			return *reinterpret_cast<class UObject* const*>(Chunk + static_cast<size_t>(Index % ElementsPerChunk) * ItemSize + ObjectOffset);
		}
		return *reinterpret_cast<class UObject* const*>(Base + static_cast<size_t>(Index) * ItemSize + ObjectOffset);
	}
};"""


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    root = sys.argv[1]
    sdk_dir = os.path.join(root, "SDK")
    marker = os.path.join(root, ".g2panel_patched")
    if os.path.exists(marker):
        print("[patch_sdk] El SDK ya estaba parcheado, nada que hacer.")
        return 0

    sizes = {}
    for m in SIZEOF_RE.finditer(read(os.path.join(root, "Assertions.inl"))):
        sizes[m.group(1)] = int(m.group(2), 16)
    print(f"[patch_sdk] {len(sizes)} tamanos de clase leidos de Assertions.inl")

    # 1) Offsets globales mutables
    basic = os.path.join(sdk_dir, "Basic.hpp")
    text = read(basic)
    text = re.sub(r"\tconstexpr int32 (GObjects|AppendString|GNames|GWorld|ProcessEvent|ProcessEventIdx)\b",
                  r"\tinline int32 \1", text)
    if "#include <cmath>" not in text:
        text = text.replace("#include <string>", "#include <string>\n#include <cmath>", 1)
    # TUObjectArray con disposicion elegida en ejecucion (plana o por bloques, tamano de elemento),
    # para que una actualizacion del juego que cambie el formato no deje el panel sin objetos.
    start = text.index("class TUObjectArray final")
    start = text.rindex("// Predefined struct TUObjectArray", 0, start)
    end = text.index("DUMPER7_ASSERTS_TUObjectArray;", start) + len("DUMPER7_ASSERTS_TUObjectArray;")
    text = text[:start] + TUOBJECTARRAY + text[end:]
    write(basic, text)

    # 2) GetWorld sin if constexpr
    engine_funcs = os.path.join(sdk_dir, "Engine_functions.cpp")
    text = read(engine_funcs)
    text = text.replace("if constexpr (Offsets::GWorld != 0)", "if (Offsets::GWorld != 0)")
    write(engine_funcs, text)

    # 5) Sombreado de parametros de plantilla en UnrealContainers.hpp
    containers = os.path.join(root, "UnrealContainers.hpp")
    text = read(containers)
    text = re.sub(r"template<typename (\w+)>\n(\s*)friend class (\w+);",
                  lambda m: f"template<typename {m.group(1)}_F>\n{m.group(2)}friend class {m.group(3)};", text)
    # SetElement vive en UC::ContainerImpl: el friend debe nombrar UC::TSet (MSVC lo tolera, GCC no).
    text = text.replace("template<typename SetElementType_F>\n\t\t\tfriend class TSet;",
                        "template<typename SetElementType_F>\n\t\t\tfriend class UC::TSet;")
    if "#include <cstring>" not in text:
        text = text.replace('#include "UtfN.hpp"', '#include "UtfN.hpp"\n#include <cstring>\n#include <cmath>', 1)
    write(containers, text)

    total_pad = 0
    total_enum = 0
    for fname in sorted(os.listdir(sdk_dir)):
        if not fname.endswith(".hpp"):
            continue
        path = os.path.join(sdk_dir, fname)
        lines = read(path).split("\n")
        lines, c_enum = fix_enums(lines)
        lines, c_pad = fix_tail_padding(lines, sizes)
        if c_enum or c_pad:
            write(path, "\n".join(lines))
        total_pad += c_pad
        total_enum += c_enum

    with open(marker, "w") as f:
        f.write("ok\n")
    print(f"[patch_sdk] Relleno final anadido en {total_pad} clases, {total_enum} valores de enum truncados.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
