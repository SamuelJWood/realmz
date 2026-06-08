/*
 * realmz_codec.js — pure big-endian codec for Realmz scenario binary records.
 *
 * Tiled-independent: operates on ArrayBuffer / Uint8Array only, so it can be
 * unit-tested under plain Node (see tools/tiled/realmz-extension/test/).
 *
 * On-disk layout is authoritative in src/realmz_orig/structs.h. All multi-byte
 * fields are big-endian (the engine byte-swaps on load via convert.h).
 *
 * Design: every decode() keeps a private copy of the original record bytes in
 * `_raw`. encode() starts from that copy and overwrites only the modeled field
 * ranges, so re-encoding an unmodified record reproduces it byte-for-byte
 * (padding / reserved bytes are preserved). This is what makes in-place saves
 * non-destructive.
 */

'use strict';

// ---- record sizes (bytes) ----------------------------------------------
var SIZE = {
  field: 90 * 90 * 2, // 16200
  doors: 100 * 40,    // 4000  (struct door[100])
  door: 40,
  randlevel: 644,
  maps: 340,
};

var GRID = 90;

// ---- big-endian primitive helpers ---------------------------------------
function rdS16(dv, o) { return dv.getInt16(o, false); }
function rdU16(dv, o) { return dv.getUint16(o, false); }
function rdS32(dv, o) { return dv.getInt32(o, false); }
function rdS8(dv, o)  { return dv.getInt8(o); }
function rdU8(dv, o)  { return dv.getUint8(o); }

function wrS16(dv, o, v) { dv.setInt16(o, v | 0, false); }
function wrS32(dv, o, v) { dv.setInt32(o, v | 0, false); }
function wrS8(dv, o, v)  { dv.setInt8(o, v | 0); }
function wrU8(dv, o, v)  { dv.setUint8(o, v & 0xff); }

function dvOf(u8) { return new DataView(u8.buffer, u8.byteOffset, u8.byteLength); }

function rdS16Arr(dv, o, n) { var a = new Array(n); for (var i = 0; i < n; i++) a[i] = rdS16(dv, o + i * 2); return a; }
function wrS16Arr(dv, o, a) { for (var i = 0; i < a.length; i++) wrS16(dv, o + i * 2, a[i]); }

// MacRoman Pascal string (length byte + chars), used for struct maps.note.
function rdPascal(u8, o, cap) {
  var len = Math.min(u8[o], cap - 1);
  var bytes = [];
  for (var i = 0; i < len; i++) bytes.push(u8[o + 1 + i]);
  return macRomanToStr(bytes);
}
// Writes [length][chars] only; bytes past the string are left as-is so a
// patched record round-trips byte-for-byte (the length byte is authoritative,
// matching how the engine reads Str255). Caller passes a buffer cloned from the
// original record.
function wrPascal(u8, o, cap, str) {
  var bytes = strToMacRoman(str).slice(0, cap - 1);
  u8[o] = bytes.length;
  for (var i = 0; i < bytes.length; i++) u8[o + 1 + i] = bytes[i];
}

// ---- field grid (90x90 int16) -------------------------------------------
// Returns a flat Int16Array of length 8100 (row-major: index = y*90 + x).
function decodeField(u8) {
  var dv = dvOf(u8);
  var out = new Int16Array(GRID * GRID);
  for (var i = 0; i < out.length; i++) out[i] = rdS16(dv, i * 2);
  return out;
}
function encodeField(grid) {
  var u8 = new Uint8Array(SIZE.field);
  var dv = dvOf(u8);
  for (var i = 0; i < GRID * GRID; i++) wrS16(dv, i * 2, grid[i]);
  return u8;
}

// ---- struct door (40 bytes) ---------------------------------------------
// 0:int32 doorid 4:landid 5:landx 6:landy 7:percent 8:short code[8] 24:short id[8]
function decodeDoor(u8, base) {
  var dv = dvOf(u8);
  return {
    doorid: rdS32(dv, base + 0),
    landid: rdS8(dv, base + 4),
    landx: rdS8(dv, base + 5),
    landy: rdS8(dv, base + 6),
    percent: rdS8(dv, base + 7),
    code: rdS16Arr(dv, base + 8, 8),
    id: rdS16Arr(dv, base + 24, 8),
    _raw: u8.slice(base, base + SIZE.door),
  };
}
function encodeDoor(door) {
  var u8 = door._raw ? door._raw.slice() : new Uint8Array(SIZE.door);
  var dv = dvOf(u8);
  wrS32(dv, 0, door.doorid);
  wrS8(dv, 4, door.landid);
  wrS8(dv, 5, door.landx);
  wrS8(dv, 6, door.landy);
  wrS8(dv, 7, door.percent);
  wrS16Arr(dv, 8, door.code);
  wrS16Arr(dv, 24, door.id);
  return u8;
}

// Decode all 100 door slots from a 4000-byte record.
function decodeDoors(u8) {
  var doors = new Array(100);
  for (var i = 0; i < 100; i++) doors[i] = decodeDoor(u8, i * SIZE.door);
  return doors;
}
function encodeDoors(doors) {
  var u8 = new Uint8Array(SIZE.doors);
  for (var i = 0; i < 100; i++) u8.set(encodeDoor(doors[i]), i * SIZE.door);
  return u8;
}

// ---- struct randlevel (644 bytes) ---------------------------------------
// offsets: 0 randrect[20] (Rect=8) | 160 percent[20] | 200 battlerange[20][2]
//   | 280 randdoor[20][3] | 400 randdoorpercent[20][3] | 520 landlook(char)
//   | 521 isdark | 522 uselos | 523 only[20] | 543 option[20] | 563 PAD
//   | 564 sound[20] | 604 text[20] = 644
var RL = {
  randrect: 0, percent: 160, battlerange: 200, randdoor: 280,
  randdoorpercent: 400, landlook: 520, isdark: 521, uselos: 522,
  only: 523, option: 543, pad: 563, sound: 564, text: 604,
};
function decodeRect(dv, o) {
  return { top: rdS16(dv, o), left: rdS16(dv, o + 2), bottom: rdS16(dv, o + 4), right: rdS16(dv, o + 6) };
}
function encodeRect(dv, o, r) {
  wrS16(dv, o, r.top); wrS16(dv, o + 2, r.left); wrS16(dv, o + 4, r.bottom); wrS16(dv, o + 6, r.right);
}
function decodeRandlevel(u8) {
  var dv = dvOf(u8);
  var rects = new Array(20), battlerange = new Array(20), randdoor = new Array(20), rdp = new Array(20);
  for (var i = 0; i < 20; i++) {
    rects[i] = decodeRect(dv, RL.randrect + i * 8);
    battlerange[i] = rdS16Arr(dv, RL.battlerange + i * 4, 2);
    randdoor[i] = rdS16Arr(dv, RL.randdoor + i * 6, 3);
    rdp[i] = rdS16Arr(dv, RL.randdoorpercent + i * 6, 3);
  }
  var only = [], option = [];
  for (var j = 0; j < 20; j++) { only.push(rdU8(dv, RL.only + j)); option.push(rdS8(dv, RL.option + j)); }
  return {
    randrect: rects,
    percent: rdS16Arr(dv, RL.percent, 20),
    battlerange: battlerange,
    randdoor: randdoor,
    randdoorpercent: rdp,
    landlook: rdS8(dv, RL.landlook),
    isdark: rdU8(dv, RL.isdark),
    uselos: rdU8(dv, RL.uselos),
    only: only,
    option: option,
    sound: rdS16Arr(dv, RL.sound, 20),
    text: rdS16Arr(dv, RL.text, 20),
    _raw: u8.slice(0, SIZE.randlevel),
  };
}
function encodeRandlevel(rl) {
  var u8 = rl._raw ? rl._raw.slice() : new Uint8Array(SIZE.randlevel);
  var dv = dvOf(u8);
  for (var i = 0; i < 20; i++) {
    encodeRect(dv, RL.randrect + i * 8, rl.randrect[i]);
    wrS16Arr(dv, RL.battlerange + i * 4, rl.battlerange[i]);
    wrS16Arr(dv, RL.randdoor + i * 6, rl.randdoor[i]);
    wrS16Arr(dv, RL.randdoorpercent + i * 6, rl.randdoorpercent[i]);
  }
  wrS16Arr(dv, RL.percent, rl.percent);
  wrS8(dv, RL.landlook, rl.landlook);
  wrU8(dv, RL.isdark, rl.isdark);
  wrU8(dv, RL.uselos, rl.uselos);
  for (var j = 0; j < 20; j++) { wrU8(dv, RL.only + j, rl.only[j]); wrS8(dv, RL.option + j, rl.option[j]); }
  wrS16Arr(dv, RL.sound, rl.sound);
  wrS16Arr(dv, RL.text, rl.text);
  return u8;
}

// ---- struct maps (340 bytes) --------------------------------------------
// icon[10][3] is 30 shorts = 60 bytes.
// 0 icon[10][3] | 60 startx | 62 starty | 64 level | 66 pictid
//   | 68 iconsize | 70 show | 72 isdungeon | 74 spare | 76 rect[4]
//   | 84 note(Str255 256) = 340
var MP = {
  icon: 0, startx: 60, starty: 62, level: 64, pictid: 66, iconsize: 68,
  show: 70, isdungeon: 72, spare: 74, rect: 76, note: 84,
};
function decodeMaps(u8) {
  var dv = dvOf(u8);
  var icon = new Array(10);
  for (var i = 0; i < 10; i++) icon[i] = rdS16Arr(dv, MP.icon + i * 6, 3);
  return {
    icon: icon,
    startx: rdS16(dv, MP.startx),
    starty: rdS16(dv, MP.starty),
    level: rdS16(dv, MP.level),
    pictid: rdS16(dv, MP.pictid),
    iconsize: rdS16(dv, MP.iconsize),
    show: rdS16(dv, MP.show),
    isdungeon: rdS16(dv, MP.isdungeon),
    spare: rdS16(dv, MP.spare),
    rect: rdS16Arr(dv, MP.rect, 4),
    note: rdPascal(u8, MP.note, 256),
    _raw: u8.slice(0, SIZE.maps),
  };
}
function encodeMaps(m) {
  var u8 = m._raw ? m._raw.slice() : new Uint8Array(SIZE.maps);
  var dv = dvOf(u8);
  for (var i = 0; i < 10; i++) wrS16Arr(dv, MP.icon + i * 6, m.icon[i]);
  wrS16(dv, MP.startx, m.startx);
  wrS16(dv, MP.starty, m.starty);
  wrS16(dv, MP.level, m.level);
  wrS16(dv, MP.pictid, m.pictid);
  wrS16(dv, MP.iconsize, m.iconsize);
  wrS16(dv, MP.show, m.show);
  wrS16(dv, MP.isdungeon, m.isdungeon);
  wrS16(dv, MP.spare, m.spare);
  wrS16Arr(dv, MP.rect, m.rect);
  wrPascal(u8, MP.note, 256, m.note);
  return u8;
}

// ---- MacRoman <-> JS string ---------------------------------------------
// Minimal MacRoman table for the 0x80-0xFF range; 0x00-0x7F are ASCII.
var MACROMAN_HIGH =
  'ÄÅÇÉÑÖÜáàâäãåçéè' +
  'êëíìîïñóòôöõúùûü' +
  '†°¢£§•¶ß®©™´¨≠ÆØ' +
  '∞±≤≥¥µ∂∑∏π∫ªºΩæø' +
  '¿¡¬√ƒ≈∆«»… ÀÃÕŒœ' +
  '–—“”‘’÷◊ÿŸ⁄€‹›ﬁﬂ' +
  '‡·‚„‰ÂÊÁËÈÍÎÏÌÓÔ' +
  'ÒÚÛÙıˆ˜¯˘˙˚¸˝˛ˇ';
function macRomanToStr(bytes) {
  var s = '';
  for (var i = 0; i < bytes.length; i++) {
    var b = bytes[i];
    s += b < 0x80 ? String.fromCharCode(b) : MACROMAN_HIGH.charAt(b - 0x80);
  }
  return s;
}
function strToMacRoman(str) {
  var out = [];
  for (var i = 0; i < str.length; i++) {
    var ch = str.charAt(i), cc = str.charCodeAt(i);
    if (cc < 0x80) { out.push(cc); continue; }
    var idx = MACROMAN_HIGH.indexOf(ch);
    out.push(idx >= 0 ? idx + 0x80 : 0x3f /* '?' */);
  }
  return out;
}

var API = {
  SIZE: SIZE, GRID: GRID,
  decodeField: decodeField, encodeField: encodeField,
  decodeDoor: decodeDoor, encodeDoor: encodeDoor,
  decodeDoors: decodeDoors, encodeDoors: encodeDoors,
  decodeRandlevel: decodeRandlevel, encodeRandlevel: encodeRandlevel,
  decodeMaps: decodeMaps, encodeMaps: encodeMaps,
  macRomanToStr: macRomanToStr, strToMacRoman: strToMacRoman,
};

if (typeof module !== 'undefined' && module.exports) module.exports = API;
// Tiled evaluates all extension scripts in one shared JS context (no globalThis,
// no require), so a top-level var is visible to the other extension files.
var RealmzCodec = API;
