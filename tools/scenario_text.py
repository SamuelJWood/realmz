#!/usr/bin/env python3
"""
Extract and re-inject human-readable text from Realmz scenario data files.

Usage:
  Extract:  python3 scenario_text.py extract <scenario_dir> <output.txt>
  Inject:   python3 scenario_text.py inject  <input.txt>    <scenario_dir>

Files handled (all use Mac Roman encoding):
  Data SD2  – story strings        (256 bytes/entry: 1-byte length + text + padding, max 255 chars)
  Data DES  – monster descriptions (256 bytes/entry, same format,                    max 255 chars)
  Data OD   – option strings        (25 bytes/entry: 1-byte length + text + padding,  max  24 chars)

The extracted .txt file uses a simple tagged format:
  ### SD2:42
  The text of entry 42 goes here.

  ### DES:7
  Another entry.

Only non-empty entries are written. To leave an entry unchanged, simply keep
its text identical to what was extracted. The inject step only rewrites entries
that are present in the .txt file.
"""

import os
import re
import sys

# (tag, filename, record_size, max_text_len)
FILES = [
    ("SD2", "Data SD2", 256, 255),
    ("DES", "Data DES", 256, 255),
    ("OD",  "Data OD",   25,  24),
]

HEADER_RE = re.compile(r'^### (\w+):(\d+)$')


# ---------------------------------------------------------------------------
# Binary I/O
# ---------------------------------------------------------------------------

def read_entries(path, record_size, max_text_len):
    """Return list of str, one per record. Empty string for empty records."""
    with open(path, "rb") as f:
        data = f.read()
    entries = []
    for i in range(len(data) // record_size):
        chunk = data[i * record_size:(i + 1) * record_size]
        length = min(chunk[0], max_text_len)
        raw = chunk[1:1 + length]
        entries.append(raw.decode("mac_roman", errors="replace").rstrip())
    return entries


def write_entries(path, updates, record_size, max_text_len):
    """
    Apply {index: new_text} updates to the binary file in-place.
    Entries not in `updates` are left byte-for-byte unchanged.
    """
    with open(path, "rb") as f:
        data = bytearray(f.read())

    for idx, text in sorted(updates.items()):
        offset = idx * record_size
        if offset + record_size > len(data):
            print(f"  WARNING: index {idx} out of range for {path}, skipping", file=sys.stderr)
            continue
        encoded = text.encode("mac_roman", errors="replace")
        if len(encoded) > max_text_len:
            print(f"  WARNING: entry {idx} is {len(encoded)} chars, truncating to {max_text_len}", file=sys.stderr)
            encoded = encoded[:max_text_len]
        length = len(encoded)
        data[offset] = length
        data[offset + 1:offset + 1 + length] = encoded
        # Zero the remainder of the text area so old bytes don't linger
        data[offset + 1 + length:offset + record_size] = b'\x00' * (record_size - 1 - length)

    with open(path, "wb") as f:
        f.write(data)


# ---------------------------------------------------------------------------
# Extract
# ---------------------------------------------------------------------------

def cmd_extract(scenario_dir, output_path):
    lines = [
        "# Realmz scenario text — Prelude to Pestilence",
        "# -----------------------------------------------",
        "# Edit the text between the ### header lines.",
        "# Do NOT modify the ### lines themselves.",
        "# Max lengths: SD2/DES = 255 chars, OD = 24 chars.",
        "# Empty entries are omitted; inject will not touch them.",
        "",
    ]

    total = 0
    for tag, filename, record_size, max_len in FILES:
        filepath = os.path.join(scenario_dir, filename)
        if not os.path.exists(filepath):
            print(f"Skipping {filename}: not found in {scenario_dir}", file=sys.stderr)
            continue

        entries = read_entries(filepath, record_size, max_len)
        written = 0
        for i, text in enumerate(entries):
            if not text.strip():
                continue  # omit blank entries
            lines.append(f"### {tag}:{i}")
            lines.append(text)
            lines.append("")  # blank separator
            written += 1
        print(f"  {filename}: {written} non-empty entries out of {len(entries)}")
        total += written

    with open(output_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))

    print(f"\nExtracted {total} entries → {output_path}")


# ---------------------------------------------------------------------------
# Inject
# ---------------------------------------------------------------------------

def cmd_inject(input_path, scenario_dir):
    with open(input_path, "r", encoding="utf-8") as f:
        content = f.read()

    # Parse: split on lines that start with ###
    parsed = {}  # (tag, idx) -> text
    current_header = None
    current_lines = []

    for line in content.splitlines():
        m = HEADER_RE.match(line)
        if m:
            # Flush previous block
            if current_header is not None:
                text = "\n".join(current_lines).strip()
                parsed[current_header] = text
            current_header = (m.group(1), int(m.group(2)))
            current_lines = []
        elif line.startswith("#"):
            continue  # comment
        else:
            if current_header is not None:
                current_lines.append(line)

    if current_header is not None:
        text = "\n".join(current_lines).strip()
        parsed[current_header] = text

    # Group by tag
    by_tag = {}
    for (tag, idx), text in parsed.items():
        by_tag.setdefault(tag, {})[idx] = text

    for tag, filename, record_size, max_len in FILES:
        if tag not in by_tag:
            continue
        filepath = os.path.join(scenario_dir, filename)
        if not os.path.exists(filepath):
            print(f"Skipping {filename}: not found", file=sys.stderr)
            continue

        # Compare to detect actual changes
        current = read_entries(filepath, record_size, max_len)
        updates = {}
        for idx, new_text in by_tag[tag].items():
            if idx >= len(current):
                print(f"  WARNING: {tag}:{idx} out of range ({len(current)} entries), skipping", file=sys.stderr)
                continue
            if current[idx] != new_text:
                updates[idx] = new_text

        if updates:
            write_entries(filepath, updates, record_size, max_len)
            print(f"  {filename}: updated {len(updates)} entries")
        else:
            print(f"  {filename}: no changes")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    if len(sys.argv) < 4 or sys.argv[1] not in ("extract", "inject"):
        print(__doc__)
        sys.exit(1)

    mode = sys.argv[1]
    if mode == "extract":
        scenario_dir, output_path = sys.argv[2], sys.argv[3]
        print(f"Extracting from: {scenario_dir}")
        cmd_extract(scenario_dir, output_path)
    else:
        input_path, scenario_dir = sys.argv[2], sys.argv[3]
        print(f"Injecting into:  {scenario_dir}")
        cmd_inject(input_path, scenario_dir)


if __name__ == "__main__":
    main()
