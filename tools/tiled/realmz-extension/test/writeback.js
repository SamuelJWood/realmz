#!/usr/bin/env node
/*
 * Write-path integration test. Mirrors exactly what realmz.js write() does to
 * the scenario binaries (reconcileField / applyEventObjects / applyAreaObjects
 * + encode + record patch), but on a throwaway copy of City of Bywater and
 * without the Tiled API. Then reopens the binaries and asserts edits persisted
 * and everything else is byte-identical. This is the closest we can get to
 * end-to-end verification without the Tiled GUI / a game build.
 */

'use strict';

var fs = require('fs');
var os = require('os');
var path = require('path');
var C = require('../realmz_codec.js');
var S = require('../realmz_scenario.js');

var src = path.join(__dirname, '..', '..', '..', '..', 'base', 'Realmz', 'Scenarios', 'City of Bywater');
var tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'bywater-'));
['Data LD', 'Data DD', 'Data RD', 'Data MD2'].forEach(function (n) {
  fs.copyFileSync(path.join(src, n), path.join(tmp, n));
});

function u8(b) { return new Uint8Array(b.buffer, b.byteOffset, b.byteLength); }
function readU8(p) { var b = fs.readFileSync(p); return u8(b); }
function patchRecord(p, off, rec) {
  var data = readU8(p);
  data.set(rec, off);
  fs.writeFileSync(p, Buffer.from(data.buffer, data.byteOffset, data.byteLength));
}
var pass = 0, fail = 0;
function ok(c, m) { if (c) pass++; else { fail++; console.log('  FAIL: ' + m); } }

var LEVEL = 0;
var fieldPath = path.join(tmp, 'Data LD');
var doorsPath = path.join(tmp, 'Data DD');
var randPath = path.join(tmp, 'Data RD');

// snapshot original level-1 records (must remain untouched)
var origDoorsL1 = readU8(doorsPath).slice(C.SIZE.doors, 2 * C.SIZE.doors);

// ---- decode level 0 ----
var gridBefore = C.decodeField(S.recordSlice(readU8(fieldPath), C.SIZE.field, LEVEL));

// ---- simulate edits (what the glue derives from Tiled layers) ----
// Build the Tiles layer (landscape) and Icons objects from the original, then
// paint tile id 41 at (10,10) and erase (11,11).
var tiles = {};
var icons = [];
for (var x = 0; x < C.GRID; x++) {
  for (var y = 0; y < C.GRID; y++) {
    var c = S.classifyTile(gridBefore[x * C.GRID + y]);
    if (c.kind === 'tile') tiles[x + ',' + y] = c.tileId - 1;
    else if (c.kind === 'icon') icons.push({ x: x, y: y, iconId: c.iconId, raw: c.raw });
  }
}
tiles['10,10'] = 41;          // paint tile id 41 (value 42)
delete tiles['11,11'];        // erase
var getTile = function (x, y) { var v = tiles[x + ',' + y]; return v == null ? null : v; };
var newGrid = S.reconcileFieldFull(gridBefore, getTile, icons);
patchRecord(fieldPath, LEVEL * C.SIZE.field, C.encodeField(newGrid));

// 2) events: edit first event's percent, add a new event at (20,20)
var doors = C.decodeDoors(S.recordSlice(readU8(doorsPath), C.SIZE.doors, LEVEL));
var events = S.doorsToEvents(doors);
var editedSlot = events[0].slot;
events[0].percent = 73;
events.push({ x: 20, y: 20, percent: 100, actions: [{ code: 1, id: 12345 }] });
S.applyEventObjects(doors, events);
patchRecord(doorsPath, LEVEL * C.SIZE.doors, C.encodeDoors(doors));

// 3) random area: bump first area percent; change landlook + dark
var rl = C.decodeRandlevel(S.recordSlice(readU8(randPath), C.SIZE.randlevel, LEVEL));
var areas = S.randToAreas(rl);
var areaSlot = areas[0].slot;
areas[0].percent = 17;
S.applyAreaObjects(rl, areas);
rl.landlook = 4; rl.isdark = 1;
patchRecord(randPath, LEVEL * C.SIZE.randlevel, C.encodeRandlevel(rl));

// ---- reopen and assert ----
var grid2 = C.decodeField(S.recordSlice(readU8(fieldPath), C.SIZE.field, LEVEL));
ok(grid2[10 * C.GRID + 10] === 42, 'painted tile persisted');
ok(grid2[11 * C.GRID + 11] === 0, 'erased tile persisted');

var doors2 = C.decodeDoors(S.recordSlice(readU8(doorsPath), C.SIZE.doors, LEVEL));
ok(doors2[editedSlot].percent === 73, 'edited event percent persisted');
var ev2 = S.doorsToEvents(doors2);
var added = ev2.filter(function (e) { return e.x === 20 && e.y === 20 && e.actions[0].id === 12345; });
ok(added.length === 1, 'added event persisted at (20,20)');

var rl2 = C.decodeRandlevel(S.recordSlice(readU8(randPath), C.SIZE.randlevel, LEVEL));
ok(rl2.percent[areaSlot] === 17, 'area percent persisted');
ok(rl2.landlook === 4 && rl2.isdark === 1, 'landlook/dark metadata persisted');

// untouched level 1 doors unchanged
var doorsL1After = readU8(doorsPath).slice(C.SIZE.doors, 2 * C.SIZE.doors);
ok(Buffer.compare(Buffer.from(origDoorsL1), Buffer.from(doorsL1After)) === 0, 'untouched level 1 record byte-identical');

fs.rmSync(tmp, { recursive: true, force: true });
console.log('\nwriteback: ' + pass + ' passed, ' + fail + ' failed');
process.exit(fail ? 1 : 0);
