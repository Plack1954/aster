#!/usr/bin/env python3
"""Generate Aster's compact, locale-independent Unicode scalar tables."""

from pathlib import Path

UNICODE_DATA = Path("/usr/share/unicode/UnicodeData.txt")
PROP_LIST = Path("/usr/share/unicode/PropList.txt")
SPECIAL_CASING = Path("/usr/share/unicode/SpecialCasing.txt")
OUTPUT = Path(__file__).resolve().parents[1] / "src" / "unicode_data.inc"


def ranges(values):
    ordered = sorted(values)
    if not ordered:
        return []
    result = []
    start = previous = ordered[0]
    for value in ordered[1:]:
        if value != previous + 1:
            result.append((start, previous))
            start = value
        previous = value
    result.append((start, previous))
    return result


categories = {}
upper = []
lower = []
for line in UNICODE_DATA.read_text(encoding="utf-8").splitlines():
    fields = line.split(";")
    code = int(fields[0], 16)
    categories[code] = fields[2]
    if fields[12]:
        upper.append((code, int(fields[12], 16)))
    if fields[13]:
        lower.append((code, int(fields[13], 16)))

white_space = set()
for raw in PROP_LIST.read_text(encoding="utf-8").splitlines():
    content = raw.split("#", 1)[0].strip()
    if not content or ";" not in content:
        continue
    span, prop = [part.strip() for part in content.split(";", 1)]
    if prop != "White_Space":
        continue
    if ".." in span:
        first, last = (int(part, 16) for part in span.split(".."))
    else:
        first = last = int(span, 16)
    white_space.update(range(first, last + 1))

sets = {
    "letter": {code for code, cat in categories.items() if cat.startswith("L")},
    "digit": {code for code, cat in categories.items() if cat == "Nd"},
    "upper": {code for code, cat in categories.items() if cat == "Lu"},
    "lower": {code for code, cat in categories.items() if cat == "Ll"},
    "white_space": white_space,
}

lines = [
    "/* Generated from the Unicode Character Database; do not edit. */",
    "typedef struct { uint32_t first, last; } AsterUnicodeRange;",
    "typedef struct { uint32_t from, to; } AsterUnicodeMapping;",
    "typedef struct { uint32_t from; uint64_t packed; } AsterUnicodeSpecial;",
]
for name, values in sets.items():
    entries = ranges(values)
    lines.append(f"static const AsterUnicodeRange aster_unicode_{name}[] = {{")
    lines.extend(f"    {{{a}U, {b}U}}," for a, b in entries)
    lines.append("};")
for name, entries in (("upper_map", upper), ("lower_map", lower)):
    lines.append(f"static const AsterUnicodeMapping aster_unicode_{name}[] = {{")
    lines.extend(f"    {{{a}U, {b}U}}," for a, b in entries)
    lines.append("};")

special = {"lower": [], "upper": []}
for raw in SPECIAL_CASING.read_text(encoding="utf-8").splitlines():
    content = raw.split("#", 1)[0].strip()
    if not content:
        continue
    fields = [part.strip() for part in content.split(";")]
    if fields[4]:
        continue
    source = int(fields[0], 16)
    for name, field in (("lower", fields[1]), ("upper", fields[3])):
        values = [int(value, 16) for value in field.split()]
        packed = sum(value << (21 * index) for index, value in enumerate(values))
        simple = dict(lower if name == "lower" else upper).get(source, source)
        if len(values) != 1 or values[0] != simple:
            special[name].append((source, packed))
for name, entries in special.items():
    entries.sort()
    lines.append(f"static const AsterUnicodeSpecial aster_unicode_special_{name}[] = {{")
    lines.extend(f"    {{{source}U, UINT64_C({packed})}}," for source, packed in entries)
    lines.append("};")

OUTPUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
