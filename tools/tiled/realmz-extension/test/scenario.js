#!/usr/bin/env node
/*
 * Model-layer test: for every level, decode doors/randlevel -> editor objects
 * (events/areas) -> apply back -> re-encode, and assert the encoded record is
 * byte-for-byte identical to the original. Validates the object<->slot
 * translation in realmz_scenario.js. Also prints a summary of what an editor
 * would see (event counts, action opcodes, random areas) for eyeballing.
 */

'use strict';

var fs = require('fs');
var path = require('path');
var C = require('../realmz_codec.js');
var S = require('../realmz_scenario.js');

var repoRoot = path.resolve(__dirname, '..', '..', '..', '..');
var scenarioDir = process.argv[2] ||
  path.join(repoRoot, 'base', 'Realmz', 'Scenarios', 'City of Bywater');
var opcodes = require('../opcodes.json').opcodes;

function u8(b) { return new Uint8Array(b.buffer, b.byteOffset, b.byteLength); }
function read(name) { var p = path.join(scenarioDir, name); return fs.existsSync(p) ? u8(fs.readFileSync(p)) : null; }
function eq(a, b) { if (a.length !== b.length) return false; for (var i = 0; i < a.length; i++) if (a[i] !== b[i]) return false; return true; }
function opname(code) { return code < 0 ? 'GOSUB ' + (-code) : (opcodes[String(code)] || ('opcode ' + code)); }

var failures = 0;

function testKind(kind) {
  var f = S.FILES[kind];
  var fieldF = read(f.field), doorsF = read(f.doors), randF = read(f.rand);
  if (!fieldF || !doorsF || !randF) { console.log('skip ' + kind + ' (missing files)'); return; }
  var n = S.countLevels(fieldF.length);
  console.log('\n=== ' + kind + ': ' + n + ' levels ===');
  for (var lvl = 0; lvl < n; lvl++) {
    var doorsRec = S.recordSlice(doorsF, C.SIZE.doors, lvl);
    var randRec = S.recordSlice(randF, C.SIZE.randlevel, lvl);

    var doors = C.decodeDoors(doorsRec);
    var rl = C.decodeRandlevel(randRec);

    // events round trip
    var events = S.doorsToEvents(doors);
    var doors2 = C.decodeDoors(doorsRec.slice()); // fresh, then patch
    S.applyEvents(events, doors2);
    var doorsOut = C.encodeDoors(doors2);
    if (!eq(doorsRec, doorsOut)) { console.log('  L' + lvl + ' FAIL doors round-trip'); failures++; }

    // areas round trip
    var areas = S.randToAreas(rl);
    var rl2 = C.decodeRandlevel(randRec.slice());
    S.applyAreas(areas, rl2);
    var randOut = C.encodeRandlevel(rl2);
    if (!eq(randRec, randOut)) { console.log('  L' + lvl + ' FAIL rand round-trip'); failures++; }

    // summary
    var actionCount = 0, samples = [];
    events.forEach(function (e) {
      e.actions.forEach(function (a) {
        if (a.code !== 0) { actionCount++; if (samples.length < 3) samples.push(opname(a.code) + '(' + a.id + ')'); }
      });
    });
    console.log('  L' + lvl + ' landlook=' + rl.landlook + ' dark=' + rl.isdark +
      ' los=' + rl.uselos + ' | events=' + events.length + ' actions=' + actionCount +
      ' areas=' + areas.length + (samples.length ? ' | e.g. ' + samples.join(', ') : ''));
  }
}

testKind('outdoor');
testKind('dungeon');

var md2 = read(S.FILES.journal);
if (md2) {
  var jm = S.readJournalMaps(md2).filter(function (m) { return m.note && m.note.trim(); });
  console.log('\n=== journal maps (Data MD2): ' + jm.length + ' with text ===');
  jm.slice(0, 5).forEach(function (m) {
    console.log('  #' + m.index + ' ->lvl ' + m.level + (m.isdungeon ? '(dung)' : '') +
      ' start=(' + m.startx + ',' + m.starty + ') "' + m.note.slice(0, 50) + '"');
  });
}

console.log('\n' + (failures ? 'FAILED (' + failures + ')' : 'PASS — events/areas translate losslessly'));
process.exit(failures ? 1 : 0);
