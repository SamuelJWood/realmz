/*
 * realmz_scenario.js — Tiled-independent scenario model layer.
 *
 * Sits on top of realmz_codec.js. Translates between raw scenario records and
 * editor-friendly structures (events, random areas, journal maps), and back.
 * Pure JS (no Tiled API) so the object<->slot translation logic — where bugs
 * would hide — is unit-testable under Node.
 *
 * A "level" is identified by (isDungeon, index). Each level is three parallel
 * records at the same index across its field/doors/rand files:
 *   outdoor: Data LD / Data DD / Data RD
 *   dungeon: Data DL / Data DDD / Data RDD
 * MD2 journal maps (Data MD2) are scenario-global, not per-level.
 */

'use strict';

// Node: require the codec. Tiled: it is a shared-context global (RealmzCodec),
// declared by realmz_codec.js which loads first (alphabetical order).
var C = (typeof require !== 'undefined' && typeof module !== 'undefined' && module.exports)
  ? require('./realmz_codec.js')
  : (typeof RealmzCodec !== 'undefined' ? RealmzCodec : null);

var FILES = {
  outdoor: { field: 'Data LD', doors: 'Data DD', rand: 'Data RD' },
  dungeon: { field: 'Data DL', doors: 'Data DDD', rand: 'Data RDD' },
  journal: 'Data MD2',
};

function countLevels(fieldFileLen) { return Math.floor(fieldFileLen / C.SIZE.field); }

function recordSlice(fileU8, recSize, index) {
  return fileU8.subarray(index * recSize, (index + 1) * recSize);
}

// ---- events (struct door[100]) <-> editor objects ------------------------
// An event is a door slot with a non-zero doorid. Position comes from the
// doorid encoding (doorid = level*10000 + x + y*100) so it matches what the
// engine looks up at runtime; landx/landy are kept too (teleport landing).
function doorPosFromId(doorid) {
  var local = ((doorid % 10000) + 10000) % 10000;
  return { x: local % 100, y: Math.floor(local / 100) };
}
// The engine pre-fills all 100 slots with a grid position, so doorid is almost
// never 0; an "event" worth showing is one with at least one non-zero action
// code. Empty-but-positioned slots stay available for adding new events (the
// glue claims a free slot and sets its doorid to level*10000 + x + y*100).
function isActionable(d) { return d.code.some(function (c) { return c !== 0; }); }
function doorsToEvents(doors) {
  var events = [];
  for (var i = 0; i < doors.length; i++) {
    var d = doors[i];
    if (d.doorid === 0 || !isActionable(d)) continue;
    var pos = doorPosFromId(d.doorid);
    var actions = [];
    for (var a = 0; a < 8; a++) actions.push({ code: d.code[a], id: d.id[a] });
    events.push({
      slot: i,
      doorid: d.doorid,
      x: pos.x, y: pos.y,
      landid: d.landid, landx: d.landx, landy: d.landy,
      percent: d.percent,
      actions: actions,
    });
  }
  return events;
}
// Apply edited events back onto the full 100-slot door array (patch in place).
function applyEvents(events, doors) {
  for (var i = 0; i < events.length; i++) {
    var e = events[i];
    var d = doors[e.slot];
    if (!d) continue;
    d.doorid = e.doorid;
    if (typeof e.landid === 'number') d.landid = e.landid;
    if (typeof e.landx === 'number') d.landx = e.landx;
    if (typeof e.landy === 'number') d.landy = e.landy;
    if (typeof e.percent === 'number') d.percent = e.percent;
    for (var a = 0; a < 8; a++) {
      d.code[a] = e.actions[a] ? e.actions[a].code : 0;
      d.id[a] = e.actions[a] ? e.actions[a].id : 0;
    }
  }
  return doors;
}

// Full reconcile of edited event objects back onto the 100-slot door array,
// handling edit / add / delete. Pure (no Tiled API), so it is unit-tested.
//   originalDoors: decoded door[] from the on-disk record (authoritative base)
//   objs: [{slot?, x, y, percent?, landid?, landx?, landy?, actions:[{code,id}]}]
// Returns the patched door[] (clone-safe: mutates a provided array).
// Behavior: existing slots are updated in place; new objects (no slot) claim a
// non-actionable slot; previously-actionable slots with no matching object are
// cleared (actions zeroed) but keep their grid position. doorid is recomputed
// from (levelNum, x, y) so moving an event updates its trigger tile.
function levelNumOf(doors) {
  for (var i = 0; i < doors.length; i++) if (doors[i].doorid !== 0) return Math.floor(doors[i].doorid / 10000);
  return 0;
}
function applyEventObjects(originalDoors, objs) {
  var doors = originalDoors;
  var levelNum = levelNumOf(doors);
  var origActionable = {};
  for (var i = 0; i < doors.length; i++) if (isActionable(doors[i])) origActionable[i] = true;

  var claimed = {}, kept = {};
  function setDoor(slot, o) {
    var d = doors[slot];
    // Preserve this slot's own level prefix (doors within one record may carry
    // different prefixes); only the position part changes when the event moves.
    var prefix = d.doorid !== 0 ? Math.floor(d.doorid / 10000) : levelNum;
    d.doorid = prefix * 10000 + (o.y | 0) * 100 + (o.x | 0);
    if (typeof o.landid === 'number') d.landid = o.landid;
    if (typeof o.landx === 'number') d.landx = o.landx;
    if (typeof o.landy === 'number') d.landy = o.landy;
    if (typeof o.percent === 'number') d.percent = o.percent;
    for (var a = 0; a < 8; a++) {
      d.code[a] = o.actions && o.actions[a] ? (o.actions[a].code | 0) : 0;
      d.id[a] = o.actions && o.actions[a] ? (o.actions[a].id | 0) : 0;
    }
  }
  // First pass: objects that reference an existing slot.
  objs.forEach(function (o) {
    if (o.slot != null && doors[o.slot]) { setDoor(o.slot, o); kept[o.slot] = true; claimed[o.slot] = true; }
  });
  // Second pass: new objects claim a free (non-actionable, unclaimed) slot.
  objs.forEach(function (o) {
    if (o.slot != null && doors[o.slot]) return;
    var slot = -1;
    for (var s = 0; s < doors.length; s++) {
      if (claimed[s] || origActionable[s]) continue;
      slot = s; break;
    }
    if (slot < 0) throw new Error('No free door slot for new event (max 100 per level)');
    claimed[slot] = true; kept[slot] = true; setDoor(slot, o);
  });
  // Deleted: previously actionable, now unreferenced -> clear actions, keep pos.
  for (var k = 0; k < doors.length; k++) {
    if (origActionable[k] && !kept[k]) {
      for (var a2 = 0; a2 < 8; a2++) { doors[k].code[a2] = 0; doors[k].id[a2] = 0; }
    }
  }
  return doors;
}

// ---- random areas (randlevel.randrect[20]) <-> editor rectangles ---------
function isRectActive(r, percent) {
  return percent > 0 || r.right > r.left || r.bottom > r.top;
}
function randToAreas(rl) {
  var areas = [];
  for (var i = 0; i < 20; i++) {
    if (!isRectActive(rl.randrect[i], rl.percent[i])) continue;
    areas.push({
      slot: i,
      top: rl.randrect[i].top, left: rl.randrect[i].left,
      bottom: rl.randrect[i].bottom, right: rl.randrect[i].right,
      percent: rl.percent[i],
      battlerange: rl.battlerange[i].slice(),
      randdoor: rl.randdoor[i].slice(),
      randdoorpercent: rl.randdoorpercent[i].slice(),
      sound: rl.sound[i], text: rl.text[i],
      option: rl.option[i], only: rl.only[i],
    });
  }
  return areas;
}
function applyAreas(areas, rl) {
  for (var i = 0; i < areas.length; i++) {
    var a = areas[i], s = a.slot;
    rl.randrect[s] = { top: a.top, left: a.left, bottom: a.bottom, right: a.right };
    rl.percent[s] = a.percent;
    if (a.battlerange) rl.battlerange[s] = a.battlerange.slice();
    if (a.randdoor) rl.randdoor[s] = a.randdoor.slice();
    if (a.randdoorpercent) rl.randdoorpercent[s] = a.randdoorpercent.slice();
    if (typeof a.sound === 'number') rl.sound[s] = a.sound;
    if (typeof a.text === 'number') rl.text[s] = a.text;
    if (typeof a.option === 'number') rl.option[s] = a.option;
    if (typeof a.only === 'number') rl.only[s] = a.only;
  }
  return rl;
}

// Full reconcile of edited random-area objects onto randlevel (edit/add/delete).
//   objs: [{slot?, top,left,bottom,right, percent?, battlerange?, randdoor?,
//           randdoorpercent?, sound?, text?, option?, only?}]
function applyAreaObjects(rl, objs) {
  var origActive = {};
  for (var i = 0; i < 20; i++) if (isRectActive(rl.randrect[i], rl.percent[i])) origActive[i] = true;
  var claimed = {}, kept = {};
  function setArea(s, a) {
    rl.randrect[s] = { top: a.top | 0, left: a.left | 0, bottom: a.bottom | 0, right: a.right | 0 };
    if (typeof a.percent === 'number') rl.percent[s] = a.percent;
    if (a.battlerange) rl.battlerange[s] = a.battlerange.slice();
    if (a.randdoor) rl.randdoor[s] = a.randdoor.slice();
    if (a.randdoorpercent) rl.randdoorpercent[s] = a.randdoorpercent.slice();
    if (typeof a.sound === 'number') rl.sound[s] = a.sound;
    if (typeof a.text === 'number') rl.text[s] = a.text;
    if (typeof a.option === 'number') rl.option[s] = a.option;
    if (typeof a.only === 'number') rl.only[s] = a.only;
  }
  objs.forEach(function (a) {
    if (a.slot != null && a.slot >= 0 && a.slot < 20) { setArea(a.slot, a); kept[a.slot] = true; claimed[a.slot] = true; }
  });
  objs.forEach(function (a) {
    if (a.slot != null && a.slot >= 0 && a.slot < 20) return;
    var slot = -1;
    for (var s = 0; s < 20; s++) { if (claimed[s] || origActive[s]) continue; slot = s; break; }
    if (slot < 0) throw new Error('No free random-area slot (max 20 per level)');
    claimed[slot] = true; kept[slot] = true; setArea(slot, a);
  });
  for (var k = 0; k < 20; k++) {
    if (origActive[k] && !kept[k]) {
      rl.randrect[k] = { top: 0, left: 0, bottom: 0, right: 0 };
      rl.percent[k] = 0;
    }
  }
  return rl;
}

// ---- field value classification (see centerpict.c) ----------------------
// A field value is one of:
//   empty (0)
//   tile  (landscape, 1..200) drawn from PICT 300+landlook; tileId is 1..200
//   icon  (a cicn color icon: buildings, objects, monsters) drawn by GetCIcon
// Positive values can carry high-bit "note"/"path" flags (bits 14/13) and up to
// three +1000 "secret" offsets; after stripping those, 1..200 are landscape and
// >200 are icon ids. Negative values are icon ids too (with -1000/-2000 variants
// for flagged versions). raw is kept so unchanged cells round-trip exactly.
function classifyTile(v) {
  if (v === 0) return { kind: 'empty', raw: 0 };
  if (v < 0) {
    var nid = v;
    if (nid < -1999) nid += 2000;
    else if (nid < -999) nid += 1000;
    return { kind: 'icon', iconId: nid, raw: v };
  }
  var w = v;
  if (w & 0x4000) w &= ~0x4000; // note flag (bit 14)
  if (w & 0x2000) w &= ~0x2000; // path flag (bit 13)
  var n = 0;
  while (w > 999 && n < 3) { w -= 1000; n++; } // secret/overlay offsets
  if (w > 200) return { kind: 'icon', iconId: w, raw: v };
  return { kind: 'tile', tileId: w, raw: v };
}

// Lossless rebuild of the 90x90 field from a Tiles layer + Icons objects.
//   originalGrid : Int16Array of the on-disk field (authoritative for preserving
//                  flag/offset bytes of unchanged cells).
//   getTile(x,y) : 0-based landscape tile id painted in Tiled, or null.
//   iconCells    : [{x, y, iconId, raw?}] from the Icons object layer.
// Rules: a painted landscape cell keeps its original bytes when the tile is
// unchanged (preserving flags), else writes the plain tile (id+1). Icon cells
// override their position: unchanged icons keep raw, new/moved/changed write the
// bare icon id. Cells with neither a tile nor an icon become 0 (erased/vacated).
function reconcileFieldFull(originalGrid, getTile, iconCells) {
  var G = C.GRID;
  var out = new Int16Array(originalGrid.length);
  for (var y = 0; y < G; y++) {
    for (var x = 0; x < G; x++) {
      var i = x * G + y;
      var t = getTile(x, y);
      if (t == null) continue; // 0 for now (icon pass may set it)
      var c = classifyTile(originalGrid[i]);
      out[i] = (c.kind === 'tile' && c.tileId === t + 1) ? originalGrid[i] : (t + 1);
    }
  }
  for (var k = 0; k < iconCells.length; k++) {
    var ic = iconCells[k];
    if (ic.x < 0 || ic.y < 0 || ic.x >= G || ic.y >= G) continue;
    var idx = ic.x * G + ic.y;
    if (ic.raw != null) {
      var rc = classifyTile(ic.raw);
      if (rc.kind === 'icon' && rc.iconId === ic.iconId) { out[idx] = ic.raw; continue; }
    }
    out[idx] = ic.iconId;
  }
  return out;
}

// ---- journal maps (struct maps in Data MD2) ------------------------------
function readJournalMaps(md2U8) {
  var n = Math.floor(md2U8.length / C.SIZE.maps), out = [];
  for (var i = 0; i < n; i++) {
    var m = C.decodeMaps(recordSlice(md2U8, C.SIZE.maps, i));
    out.push({ index: i, note: m.note, level: m.level, isdungeon: m.isdungeon,
      startx: m.startx, starty: m.starty, show: m.show, pictid: m.pictid, _maps: m });
  }
  return out;
}

var API = {
  FILES: FILES,
  countLevels: countLevels,
  recordSlice: recordSlice,
  doorPosFromId: doorPosFromId,
  isActionable: isActionable,
  doorsToEvents: doorsToEvents,
  applyEvents: applyEvents,
  applyEventObjects: applyEventObjects,
  randToAreas: randToAreas,
  applyAreas: applyAreas,
  applyAreaObjects: applyAreaObjects,
  classifyTile: classifyTile,
  reconcileFieldFull: reconcileFieldFull,
  isRectActive: isRectActive,
  readJournalMaps: readJournalMaps,
};

if (typeof module !== 'undefined' && module.exports) module.exports = API;
var RealmzScenario = API; // shared across Tiled's single JS context
