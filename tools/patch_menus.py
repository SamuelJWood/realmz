#!/usr/bin/env python3
"""
Patch The Family Jewels.rsrc:
  MENU 130 (Adventure): remove items 5-9 and items 23-31  [only if not yet applied]
  MENU 128 (Info): remove 'Realmz Order Form', 'About Divinity', 'Realmz Character Editor'
                   and trailing null/separator orphans
  MENU 137 (Preferences): remove 'Refresh Screen', 'Edit Spell Names',
                           'Edit Race/Caste Names', separator, and all version-info items
"""

import struct
from collections import OrderedDict

RSRC_PATH = "base/Realmz/Data Files/The Family Jewels.rsrc"


# ── Resource fork parser ──────────────────────────────────────────────────────

def parse_rsrc(data):
    """Return list of dicts: {type, id, name, attrs, data}"""
    data_off, map_off = struct.unpack_from('>II', data, 0)[:2]
    type_list_rel  = struct.unpack_from('>H', data, map_off + 24)[0]
    name_list_rel  = struct.unpack_from('>H', data, map_off + 26)[0]
    type_list_off  = map_off + type_list_rel
    name_list_off  = map_off + name_list_rel
    type_count     = struct.unpack_from('>H', data, type_list_off)[0] + 1
    records = []
    for i in range(type_count):
        toff = type_list_off + 2 + i * 8
        rtype     = data[toff:toff+4].decode('latin-1')
        ref_count = struct.unpack_from('>H', data, toff+4)[0] + 1
        ref_list_off = type_list_off + struct.unpack_from('>H', data, toff+6)[0]
        for j in range(ref_count):
            roff         = ref_list_off + j * 12
            res_id       = struct.unpack_from('>h', data, roff)[0]
            name_off_rel = struct.unpack_from('>H', data, roff+2)[0]
            attr_and_off = struct.unpack_from('>I', data, roff+4)[0]
            attrs        = (attr_and_off >> 24) & 0xFF
            rel_off      = attr_and_off & 0xFFFFFF
            abs_off      = data_off + rel_off
            res_len      = struct.unpack_from('>I', data, abs_off)[0]
            res_data     = data[abs_off+4 : abs_off+4+res_len]
            name = None
            if name_off_rel != 0xFFFF:
                noff = name_list_off + name_off_rel
                nlen = data[noff]
                name = data[noff+1 : noff+1+nlen].decode('mac_roman', errors='replace')
            records.append({'type': rtype, 'id': res_id,
                            'attrs': attrs, 'name': name, 'data': res_data})
    return records


def build_rsrc(records):
    """Rebuild a complete resource fork binary from resource records."""
    # ── Data section ─────────────────────────────────────────────────────────
    data_section = bytearray()
    data_offsets = []          # rel offset from data section start for each record
    for rec in records:
        data_offsets.append(len(data_section))
        payload = rec['data']
        data_section += struct.pack('>I', len(payload))
        data_section += payload
        # Pad to 4-byte alignment (standard for resource fork)
        while len(data_section) % 4:
            data_section += b'\x00'

    # ── Name list ────────────────────────────────────────────────────────────
    name_list     = bytearray()
    name_offsets  = []          # rel offset in name list per record (0xFFFF = no name)
    for rec in records:
        if rec['name'] is not None:
            name_offsets.append(len(name_list))
            nb = rec['name'].encode('mac_roman', errors='replace')
            name_list += bytes([len(nb)]) + nb
        else:
            name_offsets.append(0xFFFF)

    # ── Type list + reference lists ───────────────────────────────────────────
    by_type = OrderedDict()
    for i, rec in enumerate(records):
        by_type.setdefault(rec['type'], []).append((i, rec))

    type_count     = len(by_type)
    # type list header (2) + type entries (type_count * 8)
    type_entries_size = 2 + type_count * 8
    # ref entries follow immediately after all type entries
    # compute each ref list's offset from type list start
    ref_list_offsets = {}
    cur = type_entries_size
    for rtype, items in by_type.items():
        ref_list_offsets[rtype] = cur
        cur += len(items) * 12   # each ref entry is 12 bytes

    # Build type list bytes
    type_list = bytearray()
    type_list += struct.pack('>H', type_count - 1)
    for rtype, items in by_type.items():
        type_list += rtype.encode('latin-1')
        type_list += struct.pack('>H', len(items) - 1)
        type_list += struct.pack('>H', ref_list_offsets[rtype])

    # Build reference list bytes (in same type order)
    ref_lists = bytearray()
    for rtype, items in by_type.items():
        for idx, rec in items:
            ref_lists += struct.pack('>h', rec['id'])
            noff = name_offsets[idx]
            ref_lists += struct.pack('>H', noff)
            attr_and_off = (rec['attrs'] << 24) | data_offsets[idx]
            ref_lists += struct.pack('>I', attr_and_off)
            ref_lists += b'\x00\x00\x00\x00'  # reserved handle (4 bytes)

    # ── Map section ───────────────────────────────────────────────────────────
    data_off_val = 256          # data section always starts after 256-byte file header
    data_len_val = len(data_section)
    map_off_val  = data_off_val + data_len_val

    type_list_and_refs = bytes(type_list) + bytes(ref_lists)
    name_list_bytes    = bytes(name_list)

    type_list_offset_in_map = 28
    name_list_offset_in_map = 28 + len(type_list_and_refs)

    map_section = bytearray()
    map_section += struct.pack('>IIII',
                               data_off_val, map_off_val,
                               data_len_val, 0)   # map_len placeholder
    map_section += struct.pack('>I', 0)           # nextMap handle
    map_section += struct.pack('>H', 0)           # file ref num
    map_section += struct.pack('>H', 0)           # map attributes
    map_section += struct.pack('>H', type_list_offset_in_map)
    map_section += struct.pack('>H', name_list_offset_in_map)
    map_section += type_list_and_refs
    map_section += name_list_bytes

    map_len_val = len(map_section)
    struct.pack_into('>I', map_section, 12, map_len_val)
    struct.pack_into('>I', map_section,  4, map_off_val)

    # ── File header (256 bytes) ───────────────────────────────────────────────
    header = bytearray(256)
    struct.pack_into('>I', header,  0, data_off_val)
    struct.pack_into('>I', header,  4, map_off_val)
    struct.pack_into('>I', header,  8, data_len_val)
    struct.pack_into('>I', header, 12, map_len_val)

    return bytes(header) + bytes(data_section) + bytes(map_section)


# ── MENU resource codec ───────────────────────────────────────────────────────

def decode_menu(data):
    pos = 0
    menu_id      = struct.unpack_from('>h', data, pos)[0]; pos += 2
    width        = struct.unpack_from('>H', data, pos)[0]; pos += 2
    height       = struct.unpack_from('>H', data, pos)[0]; pos += 2
    proc_id      = struct.unpack_from('>H', data, pos)[0]; pos += 2
    reserved     = struct.unpack_from('>H', data, pos)[0]; pos += 2
    enable_flags = struct.unpack_from('>I', data, pos)[0]; pos += 4
    title_len    = data[pos]; pos += 1
    title        = data[pos:pos+title_len].decode('mac_roman', errors='replace'); pos += title_len
    items = []
    while pos < len(data):
        nlen = data[pos]; pos += 1
        if nlen == 0:
            break
        name  = data[pos:pos+nlen].decode('mac_roman', errors='replace'); pos += nlen
        icon  = data[pos]; pos += 1
        key   = data[pos]; pos += 1
        mark  = data[pos]; pos += 1
        style = data[pos]; pos += 1
        items.append({'name': name, 'icon': icon, 'key': key,
                      'mark': mark, 'style': style})
    return {'menu_id': menu_id, 'width': width, 'height': height,
            'proc_id': proc_id, 'reserved': reserved,
            'enable_flags': enable_flags, 'title': title, 'items': items}


def encode_menu(m, new_items, kept_1based):
    """Encode a MENU resource with a subset of items, re-indexing enable_flags."""
    orig_flags = m['enable_flags']
    new_flags  = orig_flags & 1   # preserve menu-title enable bit
    for new_idx, orig_idx in enumerate(kept_1based, 1):
        if new_idx > 31:
            break
        orig_bit = (orig_flags >> orig_idx) & 1 if orig_idx <= 31 else 1
        new_flags |= (orig_bit << new_idx)

    out = bytearray()
    out += struct.pack('>h', m['menu_id'])
    out += struct.pack('>H', m['width'])
    out += struct.pack('>H', m['height'])
    out += struct.pack('>H', m['proc_id'])
    out += struct.pack('>H', m['reserved'])
    out += struct.pack('>I', new_flags)
    tb = m['title'].encode('mac_roman', errors='replace')
    out += bytes([len(tb)]) + tb
    for item in new_items:
        nb = item['name'].encode('mac_roman', errors='replace')
        out += bytes([len(nb)]) + nb
        out += bytes([item['icon'], item['key'], item['mark'], item['style']])
    out += bytes([0])   # end-of-items sentinel
    return bytes(out)


def strip_trailing_junk(items):
    """Remove trailing separator ('-') and null ('\x00') items."""
    while items and items[-1]['name'] in ('-', '\x00', ''):
        items.pop()
    return items


# ── Main ─────────────────────────────────────────────────────────────────────

def main():
    with open(RSRC_PATH, 'rb') as f:
        original = f.read()

    records = parse_rsrc(original)

    for rec in records:
        if rec['type'] != 'MENU':
            continue

        # ── MENU 130 (Adventure) ─────────────────────────────────────────────
        # Remove divinity-era filler items (5-9) and extra divinity entries
        # (23-31).  Guard: skip if already patched (≤ 17 items remain).
        if rec['id'] == 130:
            m = decode_menu(rec['data'])
            if len(m['items']) > 17:
                print(f"MENU 130 (Adventure) — {len(m['items'])} items, applying patch")
                remove = set(range(5, 10)) | set(range(23, 32))
                kept   = [i for i in range(1, len(m['items'])+1) if i not in remove]
                new_items = [m['items'][i-1] for i in kept]
                rec['data'] = encode_menu(m, new_items, kept)
                print(f"  → {len(new_items)} items after")
            else:
                print(f"MENU 130 (Adventure) — already patched ({len(m['items'])} items), skipping")

        # ── MENU 128 (Info) ──────────────────────────────────────────────────
        # Remove: 'Realmz Order Form', 'About Divinity', 'Realmz Character Editor'
        # Then strip any trailing separators/null items.
        elif rec['id'] == 128:
            m = decode_menu(rec['data'])
            drop  = {'Realmz Order Form', 'About Divinity', 'Realmz Character Editor'}
            kept  = [i for i, item in enumerate(m['items'], 1)
                     if item['name'] not in drop]
            new_items = [m['items'][i-1] for i in kept]
            new_items = strip_trailing_junk(new_items)
            kept_final = [kept[i] for i in range(len(new_items))]

            removed = [item['name'] for item in m['items']
                       if item['name'] in drop or
                       (item not in new_items and item['name'] in ('-', '\x00', ''))]
            if removed or len(new_items) < len(m['items']):
                rec['data'] = encode_menu(m, new_items, kept_final)
                print(f"MENU 128 (Info): {len(m['items'])} → {len(new_items)} items")
                for i, item in enumerate(new_items, 1):
                    print(f"  {i:2d}. {item['name']!r}")
            else:
                print(f"MENU 128 (Info): no changes needed ({len(m['items'])} items)")

        # ── MENU 137 (Preferences) ───────────────────────────────────────────
        # Remove: 'Refresh Screen', 'Edit Spell Names', 'Edit Race/Caste Names',
        #         'Version Information' header, all 'X - ' version items,
        #         and the separator/trailing junk that surrounds them.
        # The section to remove starts at the first '-' separator after item 3
        # (Set Preferences) through the end of the menu.
        elif rec['id'] == 137:
            m = decode_menu(rec['data'])
            drop = {
                'Refresh Screen',
                'Edit Spell Names',
                'Edit Race/Caste Names',
                'Version Information',
                'Realmz - ',
                'Items - ',
                'Spells - ',
                'Races - ',
                'Castes - ',
                'The Family Jewels - ',
                'Realmz Character Editor - ',
            }
            # Also drop the separator that introduces the version-info section.
            # Strategy: keep items that are NOT in drop, then strip trailing junk.
            kept = [i for i, item in enumerate(m['items'], 1)
                    if item['name'] not in drop]
            new_items = [m['items'][i-1] for i in kept]
            new_items = strip_trailing_junk(new_items)
            kept_final = [kept[i] for i in range(len(new_items))]

            if len(new_items) < len(m['items']):
                rec['data'] = encode_menu(m, new_items, kept_final)
                print(f"MENU 137 (Preferences): {len(m['items'])} → {len(new_items)} items")
                for i, item in enumerate(new_items, 1):
                    print(f"  {i:2d}. {item['name']!r}")
            else:
                print(f"MENU 137 (Preferences): no changes needed ({len(m['items'])} items)")

    new_data = build_rsrc(records)

    with open(RSRC_PATH, 'wb') as f:
        f.write(new_data)
    print(f"\nWrote {len(new_data):,} bytes → {RSRC_PATH}")


if __name__ == '__main__':
    main()
