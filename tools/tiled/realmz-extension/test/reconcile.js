#!/usr/bin/env node
/*
 * Unit tests for the add/edit/delete reconcile logic that the Tiled glue
 * depends on (applyEventObjects, applyAreaObjects, reconcileField). Uses real
 * City of Bywater records as the base state.
 */

'use strict';

var fs = require('fs');
var path = require('path');
var C = require('../realmz_codec.js');
var S = require('../realmz_scenario.js');

var dir = process.argv[2] ||
  path.join(__dirname, '..', '..', '..', '..', 'base', 'Realmz', 'Scenarios', 'City of Bywater');
function u8(b) { return new Uint8Array(b.buffer, b.byteOffset, b.byteLength); }
function read(n) { return u8(fs.readFileSync(path.join(dir, n))); }

var pass = 0, fail = 0;
function ok(cond, msg) { if (cond) { pass++; } else { fail++; console.log('  FAIL: ' + msg); } }

// --- events: identity reconcile (extract -> apply unchanged) is lossless ---
(function () {
  var doorsF = read('Data DD');
  for (var lvl = 0; lvl < S.countLevels(read('Data LD').length); lvl++) {
    var rec = S.recordSlice(doorsF, C.SIZE.doors, lvl);
    var doors = C.decodeDoors(rec.slice());
    var events = S.doorsToEvents(doors);
    var doors2 = C.decodeDoors(rec.slice());
    S.applyEventObjects(doors2, events); // re-apply unchanged
    ok(eqU8(rec, C.encodeDoors(doors2)), 'L' + lvl + ' event identity reconcile');
  }
})();

// --- events: edit + add + delete behave correctly --------------------------
(function () {
  var rec = S.recordSlice(read('Data DD'), C.SIZE.doors, 0);
  var doors = C.decodeDoors(rec.slice());
  var events = S.doorsToEvents(doors);
  var before = events.length;

  // edit: change first event's percent + first action id
  events[0].percent = 42;
  events[0].actions[0] = { code: events[0].actions[0].code, id: 999 };
  // delete: drop the last event
  var deletedSlot = events[before - 1].slot;
  events.splice(before - 1, 1);
  // add: a brand-new event at tile (12,34) with a Display-text action
  events.push({ x: 12, y: 34, percent: 100, actions: [{ code: 1, id: 7 }] });

  S.applyEventObjects(doors, events);
  var back = S.doorsToEvents(doors);

  ok(back.length === before, 'count stable after edit+add+delete (' + back.length + ' vs ' + before + ')');
  ok(doors[events[0].slot].percent === 42, 'edited percent persisted');
  ok(doors[events[0].slot].id[0] === 999, 'edited action id persisted');
  // deleted slot keeps position but has no actions now
  ok(doors[deletedSlot].doorid !== 0 && !S.isActionable(doors[deletedSlot]), 'deleted event cleared, position kept');
  // added event present at (12,34)
  var added = back.filter(function (e) { return e.x === 12 && e.y === 34 && e.actions[0].code === 1 && e.actions[0].id === 7; });
  ok(added.length === 1, 'added event present at (12,34)');
})();

// --- areas: identity + edit/add/delete ------------------------------------
(function () {
  var rec = S.recordSlice(read('Data RD'), C.SIZE.randlevel, 0);
  var rl = C.decodeRandlevel(rec.slice());
  var areas = S.randToAreas(rl);
  var rl2 = C.decodeRandlevel(rec.slice());
  S.applyAreaObjects(rl2, areas);
  ok(eqU8(rec, C.encodeRandlevel(rl2)), 'area identity reconcile');

  var before = areas.length;
  areas[0].percent = 55;
  areas.push({ top: 5, left: 6, bottom: 9, right: 10, percent: 25, text: 3 });
  S.applyAreaObjects(rl, areas);
  var back = S.randToAreas(rl);
  ok(back.length === before + 1, 'area added (' + back.length + ' vs ' + (before + 1) + ')');
  ok(rl.percent[areas[0].slot] === 55, 'area percent edit persisted');
})();

// --- classifyTile: landscape vs icon vs flagged/offset --------------------
(function () {
  ok(S.classifyTile(0).kind === 'empty', 'classify 0 = empty');
  ok(S.classifyTile(5).kind === 'tile' && S.classifyTile(5).tileId === 5, 'classify 5 = landscape tile 5');
  ok(S.classifyTile(200).kind === 'tile', 'classify 200 = landscape');
  ok(S.classifyTile(1077).kind === 'tile' && S.classifyTile(1077).tileId === 77, 'classify 1077 = landscape 77 (secret offset)');
  ok(S.classifyTile(-73).kind === 'icon' && S.classifyTile(-73).iconId === -73, 'classify -73 = icon -73');
  ok(S.classifyTile(-1072).kind === 'icon' && S.classifyTile(-1072).iconId === -72, 'classify -1072 = icon -72 (offset)');
  ok(S.classifyTile(757).kind === 'icon' && S.classifyTile(757).iconId === 757, 'classify 757 = icon 757');
})();

// --- reconcileFieldFull: lossless identity on every outdoor level ----------
(function () {
  var fieldF = read('Data LD');
  for (var lvl = 0; lvl < S.countLevels(fieldF.length); lvl++) {
    var orig = C.decodeField(S.recordSlice(fieldF, C.SIZE.field, lvl));
    // emulate what the glue derives: landscape cells -> getTile, icon cells -> objects
    var icons = [];
    var tiles = {}; // "x,y" -> 0-based tile id
    for (var x = 0; x < C.GRID; x++) {
      for (var y = 0; y < C.GRID; y++) {
        var c = S.classifyTile(orig[x * C.GRID + y]);
        if (c.kind === 'tile') tiles[x + ',' + y] = c.tileId - 1;
        else if (c.kind === 'icon') icons.push({ x: x, y: y, iconId: c.iconId, raw: c.raw });
      }
    }
    var getTile = function (x, y) { var v = tiles[x + ',' + y]; return v == null ? null : v; };
    var rebuilt = S.reconcileFieldFull(orig, getTile, icons);
    var same = true;
    for (var i = 0; i < orig.length; i++) if (orig[i] !== rebuilt[i]) { same = false; break; }
    ok(same, 'L' + lvl + ' field reconcile identity (landscape+icons)');
  }
})();

// --- reconcileFieldFull: paint / erase / move-icon behavior ----------------
(function () {
  var orig = new Int16Array(C.GRID * C.GRID); // all empty
  orig[5 * C.GRID + 5] = 7;      // a landscape tile at (5,5)
  orig[6 * C.GRID + 6] = -73;    // a building icon at (6,6)
  var tiles = { '5,5': 6 /*tileId 7*/, '8,8': 40 /*new paint tileId 41*/ };
  var getTile = function (x, y) { var v = tiles[x + ',' + y]; return v == null ? null : v; };
  // move the building from (6,6) to (9,9), unchanged icon
  var icons = [{ x: 9, y: 9, iconId: -73, raw: -73 }];
  var out = S.reconcileFieldFull(orig, getTile, icons);
  ok(out[5 * C.GRID + 5] === 7, 'unchanged landscape preserved');
  ok(out[8 * C.GRID + 8] === 41, 'newly painted landscape written');
  ok(out[6 * C.GRID + 6] === 0, 'vacated building cell cleared');
  ok(out[9 * C.GRID + 9] === -73, 'moved building written at new cell');
})();

function eqU8(a, b) { if (a.length !== b.length) return false; for (var i = 0; i < a.length; i++) if (a[i] !== b[i]) return false; return true; }

console.log('\nreconcile: ' + pass + ' passed, ' + fail + ' failed');
process.exit(fail ? 1 : 0);
