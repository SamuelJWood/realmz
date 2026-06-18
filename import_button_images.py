#!/usr/bin/env python3
"""
import_button_images.py
Converts edited BMP files back into PICT resources inside The Family Jewels.rsrc.

Usage:
  python3 import_button_images.py [--155 path/to/button_155.bmp] [--197 path/to/button_197.bmp]

Defaults to C:/Users/Sam/button_155.bmp and C:/Users/Sam/button_197.bmp (WSL paths).
The resource file is patched in-place; a .bak backup is created automatically.
"""

import argparse
import os
import shutil
import struct
import sys

RSRC_PATH = os.path.join(os.path.dirname(__file__),
    "base/Realmz/Data Files/The Family Jewels.rsrc")

WIN_HOME = "/mnt/c/Users/Sam"  # default BMP source location only; script never writes here


# ── BMP reader ──────────────────────────────────────────────────────────────

def read_bmp_rgba(path: str) -> tuple[int, int, bytes]:
    """Read a 24- or 32-bit BMP; return (width, height, RGBA8888 bytes top→bottom)."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"BM":
        raise ValueError(f"Not a BMP file: {path}")
    bf_off_bits   = struct.unpack_from("<I", data, 10)[0]
    bi_width      = struct.unpack_from("<i", data, 18)[0]
    bi_height     = struct.unpack_from("<i", data, 22)[0]
    bi_bit_count  = struct.unpack_from("<H", data, 28)[0]
    bi_compression= struct.unpack_from("<I", data, 30)[0]
    if bi_compression not in (0, 3):
        raise ValueError(f"Unsupported BMP compression {bi_compression}")
    top_down = bi_height < 0
    height, width = abs(bi_height), bi_width
    bpp = bi_bit_count // 8
    stride = (width * bpp + 3) & ~3
    px = data[bf_off_bits:]
    rows = []
    for row_idx in range(height):
        src = row_idx if top_down else (height - 1 - row_idx)
        row = px[src * stride: src * stride + stride]
        out = bytearray()
        for x in range(width):
            o = x * bpp
            if bpp == 3:
                b, g, r = row[o], row[o+1], row[o+2]; a = 0xFF
            elif bpp == 4:
                b, g, r, a = row[o], row[o+1], row[o+2], row[o+3]
            else:
                raise ValueError(f"Unsupported bit depth {bi_bit_count}")
            out += bytes([a, b, g, r])  # phosg RGBA8888N: uint32 0xRRGGBBAA, LE bytes [A,B,G,R]
        rows.append(bytes(out))
    return width, height, b"".join(rows)


# ── Decoded-PICT builder ─────────────────────────────────────────────────────
#
# GetPicture() in QuickDraw.cpp recognises this custom "already-decoded" format
# by the magic header (0x0011 / 0x03FF / 0xFFFF) and skips Mac-PICT decoding.

def build_decoded_pict(width: int, height: int, rgba: bytes) -> bytes:
    assert len(rgba) == width * height * 4
    # DecodedPICTHeader layout (QuickDraw.cpp):
    #   be_uint16_t  size            → big-endian
    #   Rect bounds {top,left,bottom,right} → int16_t in NATIVE (little-endian) order
    #   be_uint16_t  version_opcode  → big-endian 0x0011
    #   be_uint16_t  version_arg     → big-endian 0x03FF
    #   be_uint16_t  data_opcode     → big-endian 0xFFFF
    header  = struct.pack(">H",     0)                      # size (be)
    header += struct.pack("<hhhh",  0, 0, height, width)    # Rect (native/le)
    header += struct.pack(">HHH",   0x0011, 0x03FF, 0xFFFF) # magic (be)
    assert len(header) == 16
    return header + rgba


# ── Mac resource-fork patcher ────────────────────────────────────────────────

def _u32be(buf, off):  return struct.unpack_from(">I", buf, off)[0]
def _s16be(buf, off):  return struct.unpack_from(">h", buf, off)[0]
def _u16be(buf, off):  return struct.unpack_from(">H", buf, off)[0]
def _set32(buf, off, v): struct.pack_into(">I", buf, off, v)
def _set16(buf, off, v): struct.pack_into(">H", buf, off, v)

def patch_pict_resource(rsrc_data: bytearray, pict_id: int, new_data: bytes) -> bytearray:
    """
    Replace PICT <pict_id> in a resource-fork image with <new_data>.
    Returns a new bytearray with the updated resource fork.
    """
    # ── read header ──
    res_data_off = _u32be(rsrc_data, 0)
    res_map_off  = _u32be(rsrc_data, 4)
    # res_data_len = _u32be(rsrc_data, 8)
    # res_map_len  = _u32be(rsrc_data, 12)

    # ── locate PICT type in the type list ──
    map_off        = res_map_off
    type_list_off  = _u16be(rsrc_data, map_off + 24)  # relative to map start
    name_list_off  = _u16be(rsrc_data, map_off + 26)
    num_types      = _s16be(rsrc_data, map_off + 28) + 1

    type_list_abs  = map_off + type_list_off   # absolute in file

    pict_ref_abs = None
    num_pict     = 0
    pict_type_entry = None
    for i in range(num_types):
        entry = type_list_abs + 2 + i * 8
        rtype = rsrc_data[entry:entry+4]
        if rtype == b'PICT':
            num_pict        = _u16be(rsrc_data, entry + 4) + 1
            ref_list_reloff = _u16be(rsrc_data, entry + 6)
            pict_ref_abs    = type_list_abs + ref_list_reloff
            pict_type_entry = entry
            break
    if pict_ref_abs is None:
        raise RuntimeError("No PICT resources found in resource fork")

    # ── find the specific PICT ID ──
    target_ref = None
    for j in range(num_pict):
        ref = pict_ref_abs + j * 12
        rid = _s16be(rsrc_data, ref)
        if rid == pict_id:
            target_ref = ref
            break
    if target_ref is None:
        raise RuntimeError(f"PICT:{pict_id} not found")

    res_attrs = rsrc_data[target_ref + 4]
    old_data_reloff = ((rsrc_data[target_ref+5] << 16) |
                       (rsrc_data[target_ref+6] <<  8) |
                        rsrc_data[target_ref+7])
    old_data_abs = res_data_off + old_data_reloff
    old_data_len = _u32be(rsrc_data, old_data_abs)

    print(f"  Found PICT:{pict_id} at data+{old_data_reloff:#x}, "
          f"current size {old_data_len}, attrs {res_attrs:#04x}")

    # ── build replacement ──
    new_len = len(new_data)
    new_block = struct.pack(">I", new_len) + new_data   # length-prefixed

    # ── splice new data block in place of old ──
    old_block_start = old_data_abs
    old_block_end   = old_data_abs + 4 + old_data_len

    result = bytearray(rsrc_data[:old_block_start])
    result += new_block
    result += rsrc_data[old_block_end:]

    delta = new_len - old_data_len   # bytes added (negative if shrunk)

    # ── update every reference whose data offset is AFTER the splice point ──
    # We need to re-find type list since offsets may have shifted if the map
    # comes after the data (it does — map_off > res_data_off in all normal files).
    # But the map itself moved if delta != 0, so recalculate.
    new_res_map_off = res_map_off + delta if res_map_off > old_block_start else res_map_off
    new_res_data_len = _u32be(rsrc_data, 8) + delta
    new_res_map_len  = _u32be(rsrc_data, 12)   # unchanged

    # Update header
    _set32(result, 0, res_data_off)
    _set32(result, 4, new_res_map_off)
    _set32(result, 8, new_res_data_len)
    _set32(result, 12, new_res_map_len)

    # Update copy of header in map
    _set32(result, new_res_map_off + 0, res_data_off)
    _set32(result, new_res_map_off + 4, new_res_map_off)
    _set32(result, new_res_map_off + 8, new_res_data_len)
    _set32(result, new_res_map_off + 12, new_res_map_len)

    # Update all reference list data-offsets that fall after the splice
    map_off2 = new_res_map_off
    type_list_off2 = _u16be(result, map_off2 + 24)
    type_list_abs2 = map_off2 + type_list_off2
    num_types2 = _s16be(result, map_off2 + 28) + 1

    for i in range(num_types2):
        entry2 = type_list_abs2 + 2 + i * 8
        num_r = _u16be(result, entry2 + 4) + 1
        ref_off = _u16be(result, entry2 + 6)
        ref_abs2 = type_list_abs2 + ref_off
        for j in range(num_r):
            ref2 = ref_abs2 + j * 12
            off_bytes = result[ref2+5:ref2+8]
            doff = (off_bytes[0] << 16) | (off_bytes[1] << 8) | off_bytes[2]
            abs_doff = res_data_off + doff
            if abs_doff > old_block_start:
                new_doff = doff + delta
                result[ref2+5] = (new_doff >> 16) & 0xFF
                result[ref2+6] = (new_doff >>  8) & 0xFF
                result[ref2+7] =  new_doff        & 0xFF

    print(f"  Replaced: old {old_data_len} bytes → new {new_len} bytes (delta {delta:+d})")
    return result


def import_pict(pict_id: int, bmp_path: str):
    print(f"\nReading {bmp_path} ...")
    width, height, rgba = read_bmp_rgba(bmp_path)
    print(f"  Size: {width}×{height}")

    blob = build_decoded_pict(width, height, rgba)

    with open(RSRC_PATH, "rb") as f:
        rsrc_data = bytearray(f.read())

    rsrc_data = patch_pict_resource(rsrc_data, pict_id, blob)

    bak_path = RSRC_PATH + ".bak"
    if not os.path.exists(bak_path):
        shutil.copy2(RSRC_PATH, bak_path)
        print(f"  Backup saved to {bak_path}")

    with open(RSRC_PATH, "wb") as f:
        f.write(rsrc_data)
    print(f"  PICT:{pict_id} updated in {RSRC_PATH}")

    # Mirror to build output directories within the repo only.
    repo = os.path.dirname(__file__)
    rsrc_name = os.path.basename(RSRC_PATH)
    mirror_dirs = [
        os.path.join(repo, "build_win",  "Data Files"),
        os.path.join(repo, "build_win",  "bin", "Data Files"),
        os.path.join(repo, "build_win",  "_CPack_Packages", "win64", "ZIP",
                     "Realmz-1.0.0-win64", "bin", "Data Files"),
        os.path.join(repo, "build_mac",  "Data Files"),
        os.path.join(repo, "build_linux", "Data Files"),
        os.path.join(repo, "build_linux", "bin", "Data Files"),
        os.path.join(repo, "build_linux", "AppDir", "usr", "bin", "Data Files"),
    ]
    for d in mirror_dirs:
        mirror = os.path.join(d, rsrc_name)
        if os.path.exists(mirror):
            shutil.copy2(RSRC_PATH, mirror)
            print(f"  Mirrored to {mirror}")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--155", dest="bmp_155",
        default=os.path.join(WIN_HOME, "button_155.bmp"),
        help="BMP for PICT 155 (dialog 200 — plain Yes/No background)")
    parser.add_argument("--197", dest="bmp_197",
        default=os.path.join(WIN_HOME, "button_197.bmp"),
        help="BMP for PICT 197 (dialog 136 — custom-text background, e.g. Swap Positions)")
    parser.add_argument("--pict", dest="pict", nargs=2, action="append",
        metavar=("ID", "PATH"), default=[],
        help="Import an arbitrary PICT: --pict ID path/to/edited.bmp "
             "(e.g. --pict -2101 restrictions.bmp for the View Restrictions screen)")
    args = parser.parse_args()

    if not os.path.exists(RSRC_PATH):
        print(f"ERROR: resource file not found:\n  {RSRC_PATH}", file=sys.stderr)
        sys.exit(1)

    jobs = [(155, args.bmp_155), (197, args.bmp_197)]
    jobs += [(int(pid), path) for pid, path in args.pict]

    any_done = False
    for pict_id, bmp_path in jobs:
        if os.path.exists(bmp_path):
            import_pict(pict_id, bmp_path)
            any_done = True
        else:
            print(f"Skipping PICT {pict_id} — file not found: {bmp_path}")

    if not any_done:
        print("No BMP files found — nothing was changed.")
        sys.exit(1)

    print("\nDone. Rebuild the project to pick up the new images.")


if __name__ == "__main__":
    main()
