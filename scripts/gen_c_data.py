#!/usr/bin/env python3
"""Generate C source with embedded daily text data for a specific month."""
import json, sys, os, textwrap

def gen_c_data(json_path, output_h, month=None):
    with open(json_path) as f:
        all_entries = json.load(f)

    if month:
        prefix = f"2026-{month:02d}"
        entries = [e for e in all_entries if e["date"].startswith(prefix)]
    else:
        entries = all_entries

    c_name = "jw_texts_data"

    lines = []
    lines.append(f"#ifndef JWTEXTSDATA_H")
    lines.append(f"#define JWTEXTSDATA_H")
    lines.append(f"")
    lines.append(f"#include <stdint.h>")
    lines.append(f"")
    lines.append(f"typedef struct {{")
    lines.append(f"    const char *date;")
    lines.append(f"    const char *ref;")
    lines.append(f"    const char *text;")
    lines.append(f"    const char *commentary;")
    lines.append(f"}} DailyTextEntry;")
    lines.append(f"")
    lines.append(f"#define DAILY_TEXT_COUNT {len(entries)}")
    lines.append(f"")

    # String table: store all strings in a single const array, use offsets
    strings = []
    for e in entries:
        strings.append(e["date"])
        strings.append(e["ref"])
        strings.append(e["text"])
        strings.append(e["commentary"])

    # Build concatenated string blob with NUL separators
    blob_parts = []
    offset = 0
    offsets = []
    for s in strings:
        blob_parts.append(s.encode("utf-8"))
        blob_parts.append(b"\0")
        offsets.append(offset)
        offset += len(s.encode("utf-8")) + 1

    blob = b"".join(blob_parts)

    lines.append(f"static const char s_text_blob[{len(blob)}] = {{")
    # Write as hex bytes, 16 per line
    for i in range(0, len(blob), 16):
        chunk = blob[i:i+16]
        hex_bytes = ", ".join(f"0x{b:02x}" for b in chunk)
        lines.append(f"    {hex_bytes},")
    lines.append(f"}};")
    lines.append(f"")

    # Entry table: store offsets into the blob
    lines.append(f"static const uint16_t s_entry_offsets[DAILY_TEXT_COUNT][4] = {{")
    for i, e in enumerate(entries):
        base = i * 4
        o_date = offsets[base]
        o_ref = offsets[base + 1]
        o_text = offsets[base + 2]
        o_comm = offsets[base + 3]
        lines.append(f"    {{ {o_date:5d}, {o_ref:5d}, {o_text:5d}, {o_comm:5d} }},")
    lines.append(f"}};")
    lines.append(f"")

    lines.append(f"static inline const char* _str_at(uint16_t offset) {{")
    lines.append(f"    return &s_text_blob[offset];")
    lines.append(f"}}")
    lines.append(f"")

    lines.append(f"static inline int find_entry_by_date(const char *date) {{")
    lines.append(f"    for (int i = 0; i < DAILY_TEXT_COUNT; i++) {{")
    lines.append(f"        if (strcmp(_str_at(s_entry_offsets[i][0]), date) == 0)")
    lines.append(f"            return i;")
    lines.append(f"    }}")
    lines.append(f"    return -1;")
    lines.append(f"}}")
    lines.append(f"")

    lines.append(f"static inline int find_entry_by_index(int index, DailyTextEntry *out) {{")
    lines.append(f"    if (index < 0 || index >= DAILY_TEXT_COUNT) return -1;")
    lines.append(f"    out->date = _str_at(s_entry_offsets[index][0]);")
    lines.append(f"    out->ref = _str_at(s_entry_offsets[index][1]);")
    lines.append(f"    out->text = _str_at(s_entry_offsets[index][2]);")
    lines.append(f"    out->commentary = _str_at(s_entry_offsets[index][3]);")
    lines.append(f"    return 0;")
    lines.append(f"}}")
    lines.append(f"")

    lines.append(f"#endif // JWTEXTSDATA_H")
    lines.append(f"")

    with open(output_h, "w") as f:
        f.write("\n".join(lines))

    print(f"Generated {output_h}: {len(entries)} entries, blob size {len(blob)} bytes")

if __name__ == "__main__":
    gen_c_data(
        json_path="resources/jw-texts.json",
        output_h="src/c/jw_texts_data.h",
        month=7
    )
