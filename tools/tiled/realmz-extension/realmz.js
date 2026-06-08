/*
 * realmz.js — Tiled extension glue. Registers a "realmz-level" map format so a
 * single Realmz scenario level can be opened, edited, and saved in Tiled.
 *
 * This is the only Tiled-API-dependent file. The heavy lifting (binary codec,
 * event/area/tile reconcile) lives in realmz_codec.js / realmz_scenario.js,
 * which are pure JS and unit-tested under Node (see test/). Load this folder as
 * a Tiled extension (Edit > Preferences > Plugins > open Extensions dir, drop
 * this folder in). open_scenario.js adds the "Open Realmz Scenario…" action.
 *
 * A ".realmz-level" file is a tiny JSON pointer written by the opener:
 *   { "scenarioDir": "/abs/path/City of Bywater", "kind": "outdoor", "index": 3 }
 */

'use strict';

// Tiled's extension loader evaluates each top-level .js file in its own scope
// and does NOT provide CommonJS require(). The sibling modules self-register on
// globalThis (RealmzCodec / RealmzScenario); we resolve them lazily inside the
// read/write callbacks (which run long after all files have loaded), so file
// load order does not matter.
var C, S, OPC;
function ensureDeps() {
  if (!C && typeof RealmzCodec !== 'undefined') C = RealmzCodec;
  if (!S && typeof RealmzScenario !== 'undefined') S = RealmzScenario;
  if (!OPC) OPC = loadOpcodes();
  if (!C || !S) throw 'Realmz codec/scenario modules not loaded';
}
function loadOpcodes() {
  try {
    var t = new TextFile(EXT_DIR + '/opcodes.json', TextFile.ReadOnly);
    var s = t.readAll(); t.close();
    return JSON.parse(s).opcodes || {};
  } catch (e) { return {}; }
}
var ICONS = null; // { "<cicn id>": {w,h} } manifest from tileset_dump
function loadIconManifest() {
  if (ICONS) return ICONS;
  try {
    var t = new TextFile(EXT_DIR + '/tilesets/icons.json', TextFile.ReadOnly);
    var s = t.readAll(); t.close();
    ICONS = JSON.parse(s);
  } catch (e) { ICONS = {}; }
  return ICONS;
}

var BASETILE_MAP = null; // { "<landlook>": <1-based tile field value> } from tileset_dump
function loadBasetileMap() {
  if (BASETILE_MAP) return BASETILE_MAP;
  try {
    var t = new TextFile(EXT_DIR + '/tilesets/landlook_basetile.json', TextFile.ReadOnly);
    var s = t.readAll(); t.close();
    BASETILE_MAP = JSON.parse(s);
  } catch (e) { BASETILE_MAP = {}; }
  return BASETILE_MAP;
}

var TILE = 32;
var EXT_DIR = (function () {
  function dirOf(p) {
    var i = Math.max(p.lastIndexOf('/'), p.lastIndexOf('\\'));
    return i >= 0 ? p.slice(0, i) : '';
  }
  // Prefer __filename when Tiled provides it.
  if (typeof __filename === 'string' && __filename) {
    var d = dirOf(__filename);
    if (d) return d;
  }
  // Fallback: parse this script's path from an Error stack (Tiled records
  // "file:///C:/.../realmz-extension/realmz.js"). Works when __filename is absent.
  try {
    throw new Error();
  } catch (e) {
    var m = (e.stack || '').match(/file:\/\/(\/[^\s]*?\.js)/);
    if (m) {
      var p = decodeURIComponent(m[1]);
      if (/^\/[A-Za-z]:/.test(p)) p = p.slice(1); // "/C:/..." -> "C:/..."
      var d2 = dirOf(p);
      if (d2) return d2;
    }
  }
  return '.';
})();

var backedUp = {}; // path -> true, one .bak per session

// ---- low-level big-endian helpers ----------------------------------------
function rdS16be(u8, off) {
  var v = ((u8[off] & 0xff) << 8) | (u8[off + 1] & 0xff);
  return v >= 0x8000 ? v - 0x10000 : v;
}
function wrS16be(u8, off, v) {
  var n = (v | 0) & 0xffff;
  u8[off] = (n >> 8) & 0xff; u8[off + 1] = n & 0xff;
}
function rdS8s(u8, off) { var b = u8[off] & 0xff; return b >= 0x80 ? b - 0x100 : b; }

// ---- text storage (Data SD2) ---------------------------------------------
// Scenario story strings: 256 bytes/entry, Mac Roman Pascal string.
// Opcode 1 ("Display text") references Data SD2[abs(id)].
var SD2_RECORD = 256;
// Opcodes whose id directly indexes Data SD2[abs(id)].
var SD2_OPCODES = { 1: true };

function decodeSd2Entry(u8, idx) {
  var off = idx * SD2_RECORD;
  if (off + SD2_RECORD > u8.length) return '';
  var len = Math.min(u8[off], 255);
  if (len === 0) return '';
  var bytes = [];
  for (var i = 0; i < len; i++) bytes.push(u8[off + 1 + i]);
  return C.macRomanToStr(bytes);
}

function encodeSd2Entry(u8, idx, str) {
  var off = idx * SD2_RECORD;
  if (off + SD2_RECORD > u8.length) return;
  var bytes = C.strToMacRoman(str.slice(0, 255));
  u8[off] = bytes.length;
  for (var i = 0; i < bytes.length; i++) u8[off + 1 + i] = bytes[i];
  for (var j = bytes.length; j < 255; j++) u8[off + 1 + j] = 0;
}

// Ensure Data SD2 has at least maxIdx+1 records (extend with zeroed blocks).
function ensureSd2Size(u8, maxIdx) {
  var needed = (maxIdx + 1) * SD2_RECORD;
  if (needed <= u8.length) return u8;
  var grown = new Uint8Array(needed);
  grown.set(u8);
  return grown;
}

// ---- extra codes (Data EDCD) ---------------------------------------------
// Most opcodes call loadextracode(id) which reads 5 signed shorts from EDCD[id].
// File: 10 bytes/entry (5 big-endian int16). 5282 entries in a typical scenario.
var EDCD_RECORD = 10;

// Returns true when the action code uses loadextracode(id) → Data EDCD[abs(id)].
// Excluded: 0 (empty), 1 (SD2 direct), 4 (ED), 5 (ED2), 6 (shop SD),
//           8 (copy in-memory door), 9 (sound id), 62 (movie), 111/112 (stack),
//           negatives (GOSUB to door slot).
function opUsesEdcd(code) {
  if (code <= 0) return false;
  switch (code) { case 1: case 4: case 5: case 6: case 8: case 9: case 62: case 111: case 112: return false; }
  return true;
}
function decodeEdcd(u8, idx) {
  var off = idx * EDCD_RECORD;
  if (off + EDCD_RECORD > u8.length) return null;
  var p = new Array(5);
  for (var i = 0; i < 5; i++) p[i] = rdS16be(u8, off + i * 2);
  return p;
}
function encodeEdcd(u8, idx, p) {
  var off = idx * EDCD_RECORD;
  for (var i = 0; i < 5; i++) wrS16be(u8, off + i * 2, p[i]);
}
function ensureEdcdSize(u8, maxIdx) {
  var needed = (maxIdx + 1) * EDCD_RECORD;
  if (needed <= u8.length) return u8;
  var g = new Uint8Array(needed); g.set(u8); return g;
}

// ---- pascal text buffer helper (shared by ED, ED2) -----------------------
function decodePascalBuf(u8, off, maxBytes) {
  var len = Math.min(u8[off], maxBytes - 1);
  if (len === 0) return '';
  var bytes = [];
  for (var i = 0; i < len; i++) bytes.push(u8[off + 1 + i]);
  return C.macRomanToStr(bytes);
}
function encodePascalBuf(u8, off, maxBytes, str) {
  var bytes = C.strToMacRoman(str.slice(0, maxBytes - 1));
  u8[off] = bytes.length;
  for (var i = 0; i < bytes.length; i++) u8[off + 1 + i] = bytes[i];
  for (var j = bytes.length; j < maxBytes - 1; j++) u8[off + 1 + j] = 0;
}

// ---- simple encounters (Data ED, struct enc) ------------------------------
// Entry layout: 106-byte struct enc + 4 × 80-byte Pascal text buffers = 426 bytes.
// struct enc fields (big-endian on disk, Mac-era packed):
//   0-31:   code[4][8]  – char, action code per choice per step
//   32-95:  id[4][8]    – int16, action id per choice per step
//   96-99:  choiceresult[4] – signed char, result tag for each choice
//   100-101: canbackout  – int16 Boolean
//   102:    maxtimes     – byte
//   103:    castesuccess – byte
//   104-105: prompt      – int16, SD2 index for the intro text
//   106…: text[0..3]     – 4 × 80-byte Pascal strings
var ED_STRUCT = 106, ED_NTEXT = 4, ED_TBUF = 80, ED_ENTRY = 426;

function decodeEnc(u8, idx, sd2U8) {
  var off = idx * ED_ENTRY;
  var enc = {
    idx: idx,
    canbackout: rdS16be(u8, off + 100),
    maxtimes:   u8[off + 102],
    prompt:     rdS16be(u8, off + 104),
    prompt_text: '',
    choices: [],
  };
  if (sd2U8 && enc.prompt !== 0)
    enc.prompt_text = decodeSd2Entry(sd2U8, Math.abs(enc.prompt));
  for (var c = 0; c < ED_NTEXT; c++) {
    var result = rdS8s(u8, off + 96 + c);
    var codes8 = [], ids8 = [];
    for (var k = 0; k < 8; k++) {
      codes8.push(rdS8s(u8, off + c * 8 + k));
      ids8.push(rdS16be(u8, off + 32 + c * 16 + k * 2));
    }
    enc.choices.push({
      text:   decodePascalBuf(u8, off + ED_STRUCT + c * ED_TBUF, ED_TBUF),
      result: result,
      codes:  codes8,
      ids:    ids8,
    });
  }
  return enc;
}
function encodeEncInPlace(u8, idx, enc) {
  var off = idx * ED_ENTRY;
  wrS16be(u8, off + 100, enc.canbackout);
  u8[off + 102] = enc.maxtimes & 0xff;
  wrS16be(u8, off + 104, enc.prompt);
  for (var c = 0; c < ED_NTEXT; c++) {
    var ch = enc.choices[c];
    u8[off + 96 + c] = ch.result & 0xff;
    for (var k = 0; k < 8; k++) {
      u8[off + c * 8 + k] = ch.codes[k] & 0xff;
      wrS16be(u8, off + 32 + c * 16 + k * 2, ch.ids[k]);
    }
    encodePascalBuf(u8, off + ED_STRUCT + c * ED_TBUF, ED_TBUF, ch.text);
  }
}

// ---- complex encounters (Data ED2, struct enc2) --------------------------
// Entry layout: 160-byte struct enc2 + 9 × 40-byte Pascal text buffers = 520 bytes.
// Key offsets (empirically verified):
//   0-31:   code[4][8]  – char action codes
//   32-95:  id[4][8]    – int16 action ids
//   96:     choiceresult – byte
//   97:     wordresult   – byte
//   98-105: group[8]
//   106-125: spellid[10] – int16
//   126-135: spellresult[10]
//   136-145: itemid[5]  – int16
//   146-150: itemresult[5]
//   151-152: canbackout – int16 Boolean
//   153-154: thief      – int16 Boolean
//   155:    maxtimes
//   156:    castesuccess
//   157:    thiefsuccess
//   158-159: prompt     – int16, SD2 index
//   160…: text[0..8]    – 9 × 40-byte Pascal strings
var ENC2_STRUCT = 160, ENC2_NTEXT = 9, ENC2_TBUF = 40, ENC2_ENTRY = 520;

function decodeEnc2(u8, idx, sd2U8) {
  var off = idx * ENC2_ENTRY;
  var enc2 = {
    idx: idx,
    canbackout: rdS16be(u8, off + 151),
    thief:      rdS16be(u8, off + 153),
    maxtimes:   u8[off + 155],
    prompt:     rdS16be(u8, off + 158),
    prompt_text: '',
    choices: [],   // same structure as enc choices
    texts: [],     // 9 labelling strings
  };
  if (sd2U8 && enc2.prompt !== 0)
    enc2.prompt_text = decodeSd2Entry(sd2U8, Math.abs(enc2.prompt));
  for (var c = 0; c < 4; c++) {
    var codes8 = [], ids8 = [];
    for (var k = 0; k < 8; k++) {
      codes8.push(rdS8s(u8, off + c * 8 + k));
      ids8.push(rdS16be(u8, off + 32 + c * 16 + k * 2));
    }
    enc2.choices.push({ codes: codes8, ids: ids8 });
  }
  for (var t = 0; t < ENC2_NTEXT; t++)
    enc2.texts.push(decodePascalBuf(u8, off + ENC2_STRUCT + t * ENC2_TBUF, ENC2_TBUF));
  return enc2;
}
function encodeEnc2InPlace(u8, idx, enc2) {
  var off = idx * ENC2_ENTRY;
  wrS16be(u8, off + 151, enc2.canbackout);
  wrS16be(u8, off + 153, enc2.thief);
  u8[off + 155] = enc2.maxtimes & 0xff;
  wrS16be(u8, off + 158, enc2.prompt);
  for (var c = 0; c < 4; c++) {
    var ch = enc2.choices[c];
    for (var k = 0; k < 8; k++) {
      u8[off + c * 8 + k] = ch.codes[k] & 0xff;
      wrS16be(u8, off + 32 + c * 16 + k * 2, ch.ids[k]);
    }
  }
  for (var t = 0; t < ENC2_NTEXT; t++)
    encodePascalBuf(u8, off + ENC2_STRUCT + t * ENC2_TBUF, ENC2_TBUF, enc2.texts[t]);
}

// ---- sub-doors (Data ED3, same struct door format as events) -------------
// Each 40-byte entry is a full struct door that can be branched to by opcodes
// using loaddoor2(id). Contains up to 8 action (code, id) pairs.
var ED3_RECORD = 40; // = C.SIZE.door

// ---- binary file helpers -------------------------------------------------
function readBytes(path) {
  var f = new BinaryFile(path, BinaryFile.ReadOnly);
  var buf = f.readAll();
  f.close();
  return new Uint8Array(buf);
}
function backupOnce(path) {
  if (backedUp[path]) return;
  if (!File.exists(path)) { backedUp[path] = true; return; }
  var bak = path + '.bak';
  if (!File.exists(bak)) {
    var src = new BinaryFile(path, BinaryFile.ReadOnly);
    var data = src.readAll(); src.close();
    var dst = new BinaryFile(bak, BinaryFile.WriteOnly);
    dst.write(data); dst.commit();
  }
  backedUp[path] = true;
}
function patchRecord(path, offset, recU8) {
  backupOnce(path);
  var f = new BinaryFile(path, BinaryFile.ReadWrite);
  f.seek(offset);
  f.write(recU8.buffer.slice(recU8.byteOffset, recU8.byteOffset + recU8.byteLength));
  f.commit();
}

// ---- pointer file --------------------------------------------------------
function readPointer(fileName) {
  var txt = '';
  var t = new TextFile(fileName, TextFile.ReadOnly);
  txt = t.readAll(); t.close();
  var p = JSON.parse(txt);
  if (!p.scenarioDir || !p.kind || p.index == null) throw 'Bad .realmz-level pointer: ' + fileName;
  return p;
}
function filesFor(kind) {
  var f = S.FILES[kind];
  if (!f) throw 'Unknown level kind: ' + kind;
  return f;
}
function join(dir, name) { return dir + '/' + name; }

// ---- tileset cache -------------------------------------------------------
var tilesetCache = {};
function tilesetFor(landlook) {
  if (tilesetCache[landlook]) return tilesetCache[landlook];
  var png = EXT_DIR + '/tilesets/landlook_' + landlook + '.png';
  var ts = new Tileset('realmz_landlook_' + landlook);
  ts.setTileSize(TILE, TILE);
  if (File.exists(png)) {
    // The PNG MUST be 20 tiles (640px) wide so that Realmz field value N maps
    // to Tiled tile id (N-1) in row-major order (see tileset_dump). columnCount
    // is then derived from the image width by loadFromImage.
    ts.loadFromImage(new Image(png), png);
  } else {
    tiled.warn('Realmz tileset image missing: ' + png +
      ' — run tools/tileset_dump to generate it. Tiles will not display.');
  }
  tilesetCache[landlook] = ts;
  return ts;
}

// ---- dungeon tileset (tiny overhead tiles from PICT 302) -----------------
// dungeon_overhead.png: 4 cols × 6 rows of 32×32 tiles (scaled from 16×16).
// Tile index N = tiny[N] from threed.c/main.c. Dungeon field values are
// bitmasks: each set bit N selects tiny[N]. We show the primary (lowest set)
// bit as the representative tile per cell.
var dungeonTilesetCache = null;
function dungeonTileset() {
  if (dungeonTilesetCache) return dungeonTilesetCache;
  var png = EXT_DIR + '/tilesets/dungeon_overhead.png';
  var ts = new Tileset('realmz_dungeon_overhead');
  ts.setTileSize(TILE, TILE);
  if (File.exists(png)) {
    ts.loadFromImage(new Image(png), png);
  } else {
    tiled.warn('Realmz dungeon tileset missing: ' + png +
      ' — run tools/tileset_dump to generate it. Dungeon tiles will not display.');
  }
  dungeonTilesetCache = ts;
  return ts;
}

// Maps a dungeon field bitmask to a tileset index (0-23 = tiny[0]..tiny[23]).
// Returns -1 for empty cells (value 0). Uses the lowest set bit among bits
// 0-12 as the primary visual indicator; tiny[7] is the solid-wall marker used
// by the game's editon rendering (threed.c:plotwall, stop=12).
function dungeonCellTile(v) {
  if (v === 0) return -1;
  for (var bit = 0; bit < 13; bit++) {
    if (v & (1 << bit)) return bit; // tiny[bit]
  }
  return -1;
}

// ---- read: build a TileMap from a level ----------------------------------
function read(fileName) {
  ensureDeps();
  var p = readPointer(fileName);
  var f = filesFor(p.kind);
  var fieldF = readBytes(join(p.scenarioDir, f.field));
  var doorsF = readBytes(join(p.scenarioDir, f.doors));
  var randF = readBytes(join(p.scenarioDir, f.rand));

  var grid = C.decodeField(S.recordSlice(fieldF, C.SIZE.field, p.index));
  var doors = C.decodeDoors(S.recordSlice(doorsF, C.SIZE.doors, p.index));
  var rl = C.decodeRandlevel(S.recordSlice(randF, C.SIZE.randlevel, p.index));

  var map = new TileMap();
  map.setSize(C.GRID, C.GRID);
  map.setTileSize(TILE, TILE);
  map.orientation = TileMap.Orthogonal;
  map.setProperty('realmz_kind', p.kind);
  map.setProperty('realmz_index', p.index);
  map.setProperty('landlook', rl.landlook);
  map.setProperty('isdark', !!rl.isdark);
  map.setProperty('uselos', !!rl.uselos);

  var isDungeon = p.kind === 'dungeon';
  var ts = isDungeon ? dungeonTileset() : tilesetFor(rl.landlook);
  map.addTileset(ts);

  // Per-map image-collection tileset for cicn icons (outdoor only).
  var man = loadIconManifest();
  var iconTs = null, iconTileById = {};
  function iconTileFor(id) {
    if (iconTileById[id] !== undefined) return iconTileById[id];
    var png = EXT_DIR + '/tilesets/icons/cicn_' + id + '.png';
    if (!man[String(id)] || !File.exists(png)) { iconTileById[id] = null; return null; }
    if (!iconTs) iconTs = new Tileset('Realmz Icons');
    var tile = iconTs.addTile();
    tile.imageFileName = png;
    iconTileById[id] = tile;
    return tile;
  }

  // Background tile drawn under outdoor icons (centerpict.c:fastplot(basetile)).
  var btMap = isDungeon ? null : loadBasetileMap();
  var basetileId = btMap ? (parseInt(btMap[String(rl.landlook)]) || 0) : 0;
  var bgTile = (basetileId > 0) ? ts.tile(basetileId - 1) : null;

  // Tiles layer + Icons layer (icons are outdoor-only; dungeon uses bitmask tiles).
  // field is short[90][90] indexed [x][y] (centerpict.c), offset = x*GRID + y.
  var layer = new TileLayer('Tiles');
  layer.width = C.GRID; layer.height = C.GRID;
  var edit = layer.edit();
  var ig = new ObjectGroup('Icons');
  for (var y = 0; y < C.GRID; y++) {
    for (var x = 0; x < C.GRID; x++) {
      var v = grid[x * C.GRID + y];
      if (isDungeon) {
        // Dungeon field values are bitmasks (threed.c:plotwall). Map the
        // primary (lowest) set bit to a tiny tile in dungeon_overhead.png.
        var tinyIdx = dungeonCellTile(v);
        if (tinyIdx >= 0) {
          var dtile = ts.tile(tinyIdx);
          if (dtile) edit.setTile(x, y, dtile);
        }
      } else {
        var cls = S.classifyTile(v);
        if (cls.kind === 'tile') {
          var tile = ts.tile(cls.tileId - 1);
          if (tile) edit.setTile(x, y, tile);
        } else if (cls.kind === 'icon') {
          if (bgTile) edit.setTile(x, y, bgTile);
          ig.addObject(iconToObject(x, y, cls, iconTileFor(cls.iconId), man[String(cls.iconId)]));
        }
      }
    }
  }
  edit.apply();
  if (iconTs) map.addTileset(iconTs);
  map.addLayer(layer);
  if (!isDungeon) map.addLayer(ig);

  // Load shared scenario data files once — used by events, encounters, sub-doors.
  var sd2Path  = join(p.scenarioDir, 'Data SD2');
  var edcdPath = join(p.scenarioDir, 'Data EDCD');
  var edPath   = join(p.scenarioDir, 'Data ED');
  var ed2Path  = join(p.scenarioDir, 'Data ED2');
  var ed3Path  = join(p.scenarioDir, 'Data ED3');
  var sd2U8  = File.exists(sd2Path)  ? readBytes(sd2Path)  : null;
  var edcdU8 = File.exists(edcdPath) ? readBytes(edcdPath) : null;
  var edU8   = File.exists(edPath)   ? readBytes(edPath)   : null;
  var ed2U8  = File.exists(ed2Path)  ? readBytes(ed2Path)  : null;
  var ed3U8  = File.exists(ed3Path)  ? readBytes(ed3Path)  : null;

  // Events — inline text (SD2) and extra-code params (EDCD) on each object.
  var events = S.doorsToEvents(doors);
  var eg = new ObjectGroup('Events');
  events.forEach(function (e) {
    var o = eventToObject(e);
    for (var a = 0; a < 8; a++) {
      var ac = e.actions[a];
      if (!ac.code) continue;
      if (SD2_OPCODES[ac.code] && ac.id !== 0 && sd2U8) {
        var txt = decodeSd2Entry(sd2U8, Math.abs(ac.id));
        if (txt) o.setProperty('text' + (a + 1), txt);
      }
      if (opUsesEdcd(ac.code) && ac.id !== 0 && edcdU8) {
        var edcdParams = decodeEdcd(edcdU8, Math.abs(ac.id));
        if (edcdParams) {
          var pfx = 'p' + (a + 1) + '_';
          for (var pi = 0; pi < 5; pi++) o.setProperty(pfx + (pi + 1), edcdParams[pi]);
        }
      }
    }
    eg.addObject(o);
  });
  map.addLayer(eg);

  // Simple Encounters (Data ED) — all entries, scenario-wide.
  if (edU8) {
    var encG = new ObjectGroup('Simple Encounters');
    var nEnc = Math.floor(edU8.length / ED_ENTRY);
    for (var ei = 0; ei < nEnc; ei++) {
      encG.addObject(encToObject(ei, edU8, sd2U8));
    }
    map.addLayer(encG);
  }

  // Complex Encounters (Data ED2) — all entries, scenario-wide.
  if (ed2U8) {
    var enc2G = new ObjectGroup('Complex Encounters');
    var nEnc2 = Math.floor(ed2U8.length / ENC2_ENTRY);
    for (var ei2 = 0; ei2 < nEnc2; ei2++) {
      enc2G.addObject(enc2ToObject(ei2, ed2U8, sd2U8));
    }
    map.addLayer(enc2G);
  }

  // Sub-Doors (Data ED3) — same struct as event doors; branched to by loaddoor2(id).
  if (ed3U8) {
    var sd3G = new ObjectGroup('Sub-Doors');
    var nSd3 = Math.floor(ed3U8.length / ED3_RECORD);
    for (var di = 0; di < nSd3; di++) {
      var dSlice = ed3U8.subarray(di * ED3_RECORD, (di + 1) * ED3_RECORD);
      var dObj = C.decodeDoor(dSlice, 0);
      var actions3 = [];
      for (var da = 0; da < 8; da++) actions3.push({ code: dObj.code[da], id: dObj.id[da] });
      var hasAction = actions3.some(function (a) { return a.code !== 0; });
      if (!hasAction) continue; // skip empty sub-doors
      var sdo = new MapObject(MapObject.Rectangle, 'subdoor ' + di);
      sdo.x = (di % 30) * TILE; sdo.y = (3000 + Math.floor(di / 30) * TILE);
      sdo.width = TILE; sdo.height = TILE;
      sdo.setProperty('subdoor_idx', di);
      for (var da2 = 0; da2 < 8; da2++) {
        sdo.setProperty('code' + (da2 + 1), actions3[da2].code);
        sdo.setProperty('id'   + (da2 + 1), actions3[da2].id);
      }
      // Inline SD2 text for display-text actions inside sub-doors.
      if (sd2U8) {
        for (var da3 = 0; da3 < 8; da3++) {
          var dac = actions3[da3];
          if (SD2_OPCODES[dac.code] && dac.id !== 0) {
            var dtxt = decodeSd2Entry(sd2U8, Math.abs(dac.id));
            if (dtxt) sdo.setProperty('text' + (da3 + 1), dtxt);
          }
        }
      }
      sd3G.addObject(sdo);
    }
    map.addLayer(sd3G);
  }

  // random areas
  var areas = S.randToAreas(rl);
  var ag = new ObjectGroup('Random Areas');
  areas.forEach(function (a) { ag.addObject(areaToObject(a)); });
  map.addLayer(ag);

  // journal maps live on outdoor level 0 (Data MD2 is scenario-global)
  if (p.kind === 'outdoor' && p.index === 0 && File.exists(join(p.scenarioDir, S.FILES.journal))) {
    var jg = new ObjectGroup('Journal Maps');
    var jms = S.readJournalMaps(readBytes(join(p.scenarioDir, S.FILES.journal)));
    jms.forEach(function (m) { jg.addObject(journalToObject(m)); });
    map.addLayer(jg);
  }

  return map;
}

// ---- object builders -----------------------------------------------------
function eventToObject(e) {
  var o = new MapObject(MapObject.Rectangle, eventName(e));
  o.x = e.x * TILE; o.y = e.y * TILE; o.width = TILE; o.height = TILE;
  o.setProperty('slot', e.slot);
  o.setProperty('percent', e.percent);
  o.setProperty('landid', e.landid);
  o.setProperty('landx', e.landx);
  o.setProperty('landy', e.landy);
  for (var a = 0; a < 8; a++) {
    o.setProperty('code' + (a + 1), e.actions[a].code);
    o.setProperty('id' + (a + 1), e.actions[a].id);
  }
  return o;
}
function eventName(e) {
  for (var a = 0; a < 8; a++) {
    var c = e.actions[a].code;
    if (c !== 0) return opLabel(c) + ' (' + e.actions[a].id + ')';
  }
  return 'event';
}
function opLabel(code) { return code < 0 ? 'GOSUB ' + (-code) : (OPC[String(code)] || ('opcode ' + code)); }

// ---- enc / enc2 object builders ------------------------------------------
function encToObject(idx, edU8, sd2U8) {
  var enc = decodeEnc(edU8, idx, sd2U8);
  var o = new MapObject(MapObject.Rectangle, 'enc ' + idx);
  o.x = (idx % 20) * TILE * 2; o.y = 3200 + Math.floor(idx / 20) * TILE * 2;
  o.width = TILE * 2; o.height = TILE * 2;
  o.setProperty('enc_idx',    idx);
  o.setProperty('prompt',     enc.prompt);
  o.setProperty('prompt_text', enc.prompt_text);
  o.setProperty('canbackout', enc.canbackout);
  o.setProperty('maxtimes',   enc.maxtimes);
  for (var c = 0; c < ED_NTEXT; c++) {
    var ch = enc.choices[c];
    var n = c + 1;
    o.setProperty('choice' + n + '_text',   ch.text);
    o.setProperty('choice' + n + '_result', ch.result);
    for (var k = 0; k < 8; k++) {
      if (ch.codes[k] !== 0) {
        o.setProperty('c' + n + '_code' + (k + 1), ch.codes[k]);
        o.setProperty('c' + n + '_id'   + (k + 1), ch.ids[k]);
      }
    }
  }
  return o;
}
function enc2ToObject(idx, ed2U8, sd2U8) {
  var enc2 = decodeEnc2(ed2U8, idx, sd2U8);
  var o = new MapObject(MapObject.Rectangle, 'enc2 ' + idx);
  o.x = (idx % 20) * TILE * 2; o.y = 3600 + Math.floor(idx / 20) * TILE * 2;
  o.width = TILE * 2; o.height = TILE * 2;
  o.setProperty('enc2_idx',   idx);
  o.setProperty('prompt',     enc2.prompt);
  o.setProperty('prompt_text', enc2.prompt_text);
  o.setProperty('canbackout', enc2.canbackout);
  o.setProperty('thief',      enc2.thief);
  o.setProperty('maxtimes',   enc2.maxtimes);
  for (var t = 0; t < ENC2_NTEXT; t++) {
    if (enc2.texts[t].trim()) o.setProperty('text' + (t + 1), enc2.texts[t]);
  }
  for (var c = 0; c < 4; c++) {
    var ch = enc2.choices[c];
    for (var k = 0; k < 8; k++) {
      if (ch.codes[k] !== 0) {
        o.setProperty('c' + (c+1) + '_code' + (k + 1), ch.codes[k]);
        o.setProperty('c' + (c+1) + '_id'   + (k + 1), ch.ids[k]);
      }
    }
  }
  return o;
}

// A cicn icon cell (building/object). Tile-object showing the icon image, with
// iconId + raw stored so the field value round-trips losslessly on save.
function iconToObject(x, y, cls, tile, meta) {
  var o = new MapObject(MapObject.Rectangle, 'icon ' + cls.iconId);
  if (tile) {
    o.tile = tile;
    o.width = (meta && meta.w) || TILE;
    o.height = (meta && meta.h) || TILE;
  } else {
    o.width = TILE; o.height = TILE; // image missing: still editable as a marker
  }
  // Tile objects anchor at their bottom-left corner, so the image top sits at
  // y*TILE when o.y = y*TILE + height. write() inverts this.
  o.x = x * TILE;
  o.y = y * TILE + o.height;
  o.setProperty('iconId', cls.iconId);
  o.setProperty('raw', cls.raw);
  return o;
}

function areaToObject(a) {
  var o = new MapObject(MapObject.Rectangle, 'random ' + a.percent + '%');
  o.x = a.left * TILE; o.y = a.top * TILE;
  o.width = Math.max(1, a.right - a.left) * TILE;
  o.height = Math.max(1, a.bottom - a.top) * TILE;
  o.setProperty('slot', a.slot);
  o.setProperty('percent', a.percent);
  o.setProperty('battle_min', a.battlerange[0]);
  o.setProperty('battle_max', a.battlerange[1]);
  o.setProperty('sound', a.sound);
  o.setProperty('text', a.text);
  o.setProperty('option', a.option);
  o.setProperty('only', a.only);
  for (var i = 0; i < 3; i++) {
    o.setProperty('randdoor' + (i + 1), a.randdoor[i]);
    o.setProperty('randdoorpct' + (i + 1), a.randdoorpercent[i]);
  }
  return o;
}
function journalToObject(m) {
  var o = new MapObject(MapObject.Point, 'map ' + m.index);
  o.x = m.startx * TILE; o.y = m.starty * TILE;
  o.setProperty('index', m.index);
  o.setProperty('note', m.note);
  o.setProperty('target_level', m.level);
  o.setProperty('isdungeon', m.isdungeon);
  o.setProperty('startx', m.startx);
  o.setProperty('starty', m.starty);
  o.setProperty('show', m.show);
  o.setProperty('pictid', m.pictid);
  return o;
}

// ---- write: persist a TileMap back into the scenario binaries -------------
function layerByName(map, name) {
  for (var i = 0; i < map.layerCount; i++) {
    var l = map.layerAt(i);
    if (l.name === name) return l;
  }
  return null;
}
function prop(o, name, dflt) { var v = o.property(name); return v == null ? dflt : v; }

function write(map, fileName) {
  try {
    ensureDeps();
    var p = readPointer(fileName);
    var f = filesFor(p.kind);
    var fieldPath = join(p.scenarioDir, f.field);
    var doorsPath = join(p.scenarioDir, f.doors);
    var randPath = join(p.scenarioDir, f.rand);

    var fieldF = readBytes(fieldPath);
    var doorsF = readBytes(doorsPath);
    var randF = readBytes(randPath);

    // --- tiles + icons (outdoor only; dungeon field is a bitmask, not tile IDs) ---
    var origGrid = C.decodeField(S.recordSlice(fieldF, C.SIZE.field, p.index));
    if (p.kind !== 'dungeon') {
      var tl = layerByName(map, 'Tiles');
      var getTile = function (x, y) {
        var t = tl ? tl.tileAt(x, y) : null;
        return t ? t.id : null; // 0-based id within the landscape tileset
      };
      var ig = layerByName(map, 'Icons');
      var iconCells = [];
      if (ig) {
        for (var ii = 0; ii < ig.objectCount; ii++) {
          var io = ig.objectAt(ii);
          var rawProp = io.property('raw');
          iconCells.push({
            x: Math.round(io.x / TILE), y: Math.round((io.y - io.height) / TILE),
            iconId: prop(io, 'iconId', 0) | 0,
            raw: rawProp == null ? null : (rawProp | 0),
          });
        }
      }
      var newGrid = S.reconcileFieldFull(origGrid, getTile, iconCells);
      patchRecord(fieldPath, p.index * C.SIZE.field, C.encodeField(newGrid));
    }
    // Dungeon field values are bitmasks managed by threed.c; we never overwrite them.

    // --- events ---
    var doors = C.decodeDoors(S.recordSlice(doorsF, C.SIZE.doors, p.index));
    var eg = layerByName(map, 'Events');
    if (eg) {
      var evObjs = [];
      for (var i = 0; i < eg.objectCount; i++) evObjs.push(objectToEvent(eg.objectAt(i)));
      S.applyEventObjects(doors, evObjs);
    }
    patchRecord(doorsPath, p.index * C.SIZE.doors, C.encodeDoors(doors));

    // --- story text (Data SD2) — accumulated from events AND encounter prompt_text edits ---
    var sd2FilePath = join(p.scenarioDir, 'Data SD2');
    var sd2 = File.exists(sd2FilePath) ? readBytes(sd2FilePath) : null;
    var sd2Dirty = false;
    if (eg && sd2) {
      for (var ei = 0; ei < eg.objectCount; ei++) {
        var eo = eg.objectAt(ei);
        for (var ea = 0; ea < 8; ea++) {
          var eCode = prop(eo, 'code' + (ea + 1), 0) | 0;
          var eId   = prop(eo, 'id'   + (ea + 1), 0) | 0;
          var eTxtProp = eo.property('text' + (ea + 1));
          if (SD2_OPCODES[eCode] && eId !== 0 && eTxtProp != null) {
            var eTxtIdx = Math.abs(eId);
            sd2 = ensureSd2Size(sd2, eTxtIdx);
            encodeSd2Entry(sd2, eTxtIdx, String(eTxtProp));
            sd2Dirty = true;
          }
        }
      }
    }
    // SD2 is flushed at the end after all sections have had a chance to write.

    // --- extra codes (Data EDCD) — written for EDCD-using actions with p<N>_1..5 ---
    var edcdFilePath = join(p.scenarioDir, 'Data EDCD');
    if (eg && File.exists(edcdFilePath)) {
      var edcd = readBytes(edcdFilePath);
      var edcdDirty = false;
      for (var xei = 0; xei < eg.objectCount; xei++) {
        var xeo = eg.objectAt(xei);
        for (var xea = 0; xea < 8; xea++) {
          var xCode = prop(xeo, 'code' + (xea + 1), 0) | 0;
          var xId   = prop(xeo, 'id'   + (xea + 1), 0) | 0;
          if (!opUsesEdcd(xCode) || xId === 0) continue;
          var pfx = 'p' + (xea + 1) + '_';
          if (xeo.property(pfx + '1') == null) continue; // no edcd props
          var xIdx = Math.abs(xId);
          edcd = ensureEdcdSize(edcd, xIdx);
          encodeEdcd(edcd, xIdx, [
            prop(xeo, pfx + '1', 0) | 0, prop(xeo, pfx + '2', 0) | 0,
            prop(xeo, pfx + '3', 0) | 0, prop(xeo, pfx + '4', 0) | 0,
            prop(xeo, pfx + '5', 0) | 0,
          ]);
          edcdDirty = true;
        }
      }
      if (edcdDirty) {
        backupOnce(edcdFilePath);
        var edcdOut = new BinaryFile(edcdFilePath, BinaryFile.ReadWrite);
        edcdOut.seek(0);
        edcdOut.write(edcd.buffer.slice(edcd.byteOffset, edcd.byteOffset + edcd.byteLength));
        edcdOut.commit();
      }
    }

    // --- simple encounters (Data ED) — from the Simple Encounters layer ---
    var edFilePath = join(p.scenarioDir, 'Data ED');
    var encG = layerByName(map, 'Simple Encounters');
    if (encG && File.exists(edFilePath)) {
      var edFile = readBytes(edFilePath);
      var edDirty = false;
      for (var eni = 0; eni < encG.objectCount; eni++) {
        var eno = encG.objectAt(eni);
        var enIdx = prop(eno, 'enc_idx', -1) | 0;
        if (enIdx < 0 || (enIdx + 1) * ED_ENTRY > edFile.length) continue;
        var enPrompt = prop(eno, 'prompt', 0) | 0;
        var enCb = prop(eno, 'canbackout', 0) | 0;
        var enMt = prop(eno, 'maxtimes', 0) | 0;
        var enChoices = [];
        for (var ec = 0; ec < ED_NTEXT; ec++) {
          var ecn = ec + 1;
          // Read existing struct to preserve action codes/ids
          var existing = decodeEnc(edFile, enIdx, null);
          var ch = existing.choices[ec];
          ch.text   = String(prop(eno, 'choice' + ecn + '_text', ch.text));
          ch.result = prop(eno, 'choice' + ecn + '_result', ch.result) | 0;
          for (var ek = 0; ek < 8; ek++) {
            var cProp = eno.property('c' + ecn + '_code' + (ek + 1));
            var iProp = eno.property('c' + ecn + '_id'   + (ek + 1));
            if (cProp != null) ch.codes[ek] = cProp | 0;
            if (iProp != null) ch.ids[ek]   = iProp | 0;
          }
          enChoices.push(ch);
        }
        encodeEncInPlace(edFile, enIdx, {
          prompt: enPrompt, canbackout: enCb, maxtimes: enMt, choices: enChoices,
        });
        // Propagate prompt_text to SD2 if edited
        var enTxtProp = eno.property('prompt_text');
        if (enTxtProp != null && enPrompt !== 0 && sd2) {
          var enSD2Idx = Math.abs(enPrompt);
          sd2 = ensureSd2Size(sd2, enSD2Idx);
          encodeSd2Entry(sd2, enSD2Idx, String(enTxtProp));
          sd2Dirty = true;
        }
        edDirty = true;
      }
      if (edDirty) {
        backupOnce(edFilePath);
        var edOut = new BinaryFile(edFilePath, BinaryFile.ReadWrite);
        edOut.seek(0);
        edOut.write(edFile.buffer.slice(edFile.byteOffset, edFile.byteOffset + edFile.byteLength));
        edOut.commit();
      }
    }

    // --- complex encounters (Data ED2) — from the Complex Encounters layer ---
    var ed2FilePath = join(p.scenarioDir, 'Data ED2');
    var enc2G = layerByName(map, 'Complex Encounters');
    if (enc2G && File.exists(ed2FilePath)) {
      var ed2File = readBytes(ed2FilePath);
      var ed2Dirty = false;
      for (var e2i = 0; e2i < enc2G.objectCount; e2i++) {
        var e2o = enc2G.objectAt(e2i);
        var e2Idx = prop(e2o, 'enc2_idx', -1) | 0;
        if (e2Idx < 0 || (e2Idx + 1) * ENC2_ENTRY > ed2File.length) continue;
        var existing2 = decodeEnc2(ed2File, e2Idx, null);
        existing2.prompt     = prop(e2o, 'prompt',     existing2.prompt)     | 0;
        existing2.canbackout = prop(e2o, 'canbackout', existing2.canbackout) | 0;
        existing2.thief      = prop(e2o, 'thief',      existing2.thief)      | 0;
        existing2.maxtimes   = prop(e2o, 'maxtimes',   existing2.maxtimes)   | 0;
        for (var et = 0; et < ENC2_NTEXT; et++) {
          var etProp = e2o.property('text' + (et + 1));
          if (etProp != null) existing2.texts[et] = String(etProp);
        }
        for (var e2c = 0; e2c < 4; e2c++) {
          for (var e2k = 0; e2k < 8; e2k++) {
            var e2cProp = e2o.property('c' + (e2c+1) + '_code' + (e2k+1));
            var e2iProp = e2o.property('c' + (e2c+1) + '_id'   + (e2k+1));
            if (e2cProp != null) existing2.choices[e2c].codes[e2k] = e2cProp | 0;
            if (e2iProp != null) existing2.choices[e2c].ids[e2k]   = e2iProp | 0;
          }
        }
        encodeEnc2InPlace(ed2File, e2Idx, existing2);
        var e2TxtProp = e2o.property('prompt_text');
        if (e2TxtProp != null && existing2.prompt !== 0 && sd2) {
          var e2SD2Idx = Math.abs(existing2.prompt);
          sd2 = ensureSd2Size(sd2, e2SD2Idx);
          encodeSd2Entry(sd2, e2SD2Idx, String(e2TxtProp));
          sd2Dirty = true;
        }
        ed2Dirty = true;
      }
      if (ed2Dirty) {
        backupOnce(ed2FilePath);
        var ed2Out = new BinaryFile(ed2FilePath, BinaryFile.ReadWrite);
        ed2Out.seek(0);
        ed2Out.write(ed2File.buffer.slice(ed2File.byteOffset, ed2File.byteOffset + ed2File.byteLength));
        ed2Out.commit();
      }
    }

    // --- sub-doors (Data ED3) — from the Sub-Doors layer ---
    var ed3FilePath = join(p.scenarioDir, 'Data ED3');
    var sd3G = layerByName(map, 'Sub-Doors');
    if (sd3G && File.exists(ed3FilePath)) {
      var ed3File = readBytes(ed3FilePath);
      var ed3Dirty = false;
      for (var s3i = 0; s3i < sd3G.objectCount; s3i++) {
        var s3o = sd3G.objectAt(s3i);
        var s3Idx = prop(s3o, 'subdoor_idx', -1) | 0;
        if (s3Idx < 0 || (s3Idx + 1) * ED3_RECORD > ed3File.length) continue;
        var d3Slice = ed3File.subarray(s3Idx * ED3_RECORD, (s3Idx + 1) * ED3_RECORD);
        var d3Obj = C.decodeDoor(d3Slice, 0);
        for (var s3a = 0; s3a < 8; s3a++) {
          d3Obj.code[s3a] = prop(s3o, 'code' + (s3a + 1), d3Obj.code[s3a]) | 0;
          d3Obj.id[s3a]   = prop(s3o, 'id'   + (s3a + 1), d3Obj.id[s3a])   | 0;
        }
        var d3Enc = C.encodeDoor(d3Obj);
        for (var s3b = 0; s3b < ED3_RECORD; s3b++) ed3File[s3Idx * ED3_RECORD + s3b] = d3Enc[s3b];
        ed3Dirty = true;
      }
      if (ed3Dirty) {
        backupOnce(ed3FilePath);
        var ed3Out = new BinaryFile(ed3FilePath, BinaryFile.ReadWrite);
        ed3Out.seek(0);
        ed3Out.write(ed3File.buffer.slice(ed3File.byteOffset, ed3File.byteOffset + ed3File.byteLength));
        ed3Out.commit();
      }
    }

    // --- flush accumulated SD2 changes (from events + encounters) ---
    if (sd2Dirty && sd2 && File.exists(sd2FilePath)) {
      backupOnce(sd2FilePath);
      var sd2Out = new BinaryFile(sd2FilePath, BinaryFile.ReadWrite);
      sd2Out.seek(0);
      sd2Out.write(sd2.buffer.slice(sd2.byteOffset, sd2.byteOffset + sd2.byteLength));
      sd2Out.commit();
    }

    // --- random areas + per-level metadata ---
    var rl = C.decodeRandlevel(S.recordSlice(randF, C.SIZE.randlevel, p.index));
    var ag = layerByName(map, 'Random Areas');
    if (ag) {
      var arObjs = [];
      for (var j = 0; j < ag.objectCount; j++) arObjs.push(objectToArea(ag.objectAt(j)));
      S.applyAreaObjects(rl, arObjs);
    }
    rl.landlook = prop(map, 'landlook', rl.landlook) | 0;
    rl.isdark = prop(map, 'isdark', !!rl.isdark) ? 1 : 0;
    rl.uselos = prop(map, 'uselos', !!rl.uselos) ? 1 : 0;
    patchRecord(randPath, p.index * C.SIZE.randlevel, C.encodeRandlevel(rl));

    // --- journal maps (only when editing outdoor level 0) ---
    var jg = layerByName(map, 'Journal Maps');
    if (jg && p.kind === 'outdoor' && p.index === 0) {
      var md2Path = join(p.scenarioDir, S.FILES.journal);
      var md2 = readBytes(md2Path);
      backupOnce(md2Path);
      for (var k = 0; k < jg.objectCount; k++) {
        var jo = jg.objectAt(k);
        var idx = prop(jo, 'index', -1) | 0;
        if (idx < 0 || (idx + 1) * C.SIZE.maps > md2.length) continue;
        var m = C.decodeMaps(S.recordSlice(md2, C.SIZE.maps, idx));
        m.note = String(prop(jo, 'note', m.note));
        m.level = prop(jo, 'target_level', m.level) | 0;
        m.isdungeon = prop(jo, 'isdungeon', m.isdungeon) | 0;
        m.startx = prop(jo, 'startx', m.startx) | 0;
        m.starty = prop(jo, 'starty', m.starty) | 0;
        m.show = prop(jo, 'show', m.show) | 0;
        m.pictid = prop(jo, 'pictid', m.pictid) | 0;
        var enc = C.encodeMaps(m);
        md2.set(enc, idx * C.SIZE.maps);
      }
      var out = new BinaryFile(md2Path, BinaryFile.ReadWrite);
      out.seek(0); out.write(md2.buffer.slice(0, md2.byteLength)); out.commit();
    }

    return '';
  } catch (err) {
    return 'Realmz save failed: ' + err;
  }
}

function objectToEvent(o) {
  var slot = o.property('slot');
  var actions = [];
  for (var a = 0; a < 8; a++) {
    actions.push({ code: prop(o, 'code' + (a + 1), 0) | 0, id: prop(o, 'id' + (a + 1), 0) | 0 });
  }
  return {
    slot: (slot == null ? null : slot | 0),
    x: Math.round(o.x / TILE), y: Math.round(o.y / TILE),
    percent: prop(o, 'percent', 0) | 0,
    landid: prop(o, 'landid', 0) | 0,
    landx: prop(o, 'landx', 0) | 0,
    landy: prop(o, 'landy', 0) | 0,
    actions: actions,
  };
}
function objectToArea(o) {
  var slot = o.property('slot');
  var left = Math.round(o.x / TILE), top = Math.round(o.y / TILE);
  return {
    slot: (slot == null ? null : slot | 0),
    left: left, top: top,
    right: left + Math.round(o.width / TILE),
    bottom: top + Math.round(o.height / TILE),
    percent: prop(o, 'percent', 0) | 0,
    battlerange: [prop(o, 'battle_min', 0) | 0, prop(o, 'battle_max', 0) | 0],
    randdoor: [prop(o, 'randdoor1', 0) | 0, prop(o, 'randdoor2', 0) | 0, prop(o, 'randdoor3', 0) | 0],
    randdoorpercent: [prop(o, 'randdoorpct1', 0) | 0, prop(o, 'randdoorpct2', 0) | 0, prop(o, 'randdoorpct3', 0) | 0],
    sound: prop(o, 'sound', 0) | 0,
    text: prop(o, 'text', 0) | 0,
    option: prop(o, 'option', 0) | 0,
    only: prop(o, 'only', 0) | 0,
  };
}

tiled.registerMapFormat('realmz-level', {
  name: 'Realmz scenario level',
  extension: 'realmz-level',
  supportsFile: function (fileName) { return /\.realmz-level$/i.test(fileName); },
  read: read,
  write: write,
});

// shared-context handle (not required by the opener, kept for debugging)
var Realmz = { read: read, write: write, EXT_DIR: EXT_DIR };
