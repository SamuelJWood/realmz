#!/usr/bin/env node
/*
 * Round-trip test: decode then re-encode every record of a scenario's map
 * files and assert byte-for-byte equality. Validates that the codec's field
 * offsets are self-consistent and the write path is non-destructive
 * (verification step 1).
 *
 * Usage: node roundtrip.js [scenarioDir]
 *   default scenarioDir = base/Realmz/Scenarios/City of Bywater
 */

'use strict';

var fs = require('fs');
var path = require('path');
var C = require('../realmz_codec.js');

var repoRoot = path.resolve(__dirname, '..', '..', '..', '..');
var scenarioDir = process.argv[2] ||
  path.join(repoRoot, 'base', 'Realmz', 'Scenarios', 'City of Bywater');

function u8(buf) { return new Uint8Array(buf.buffer, buf.byteOffset, buf.byteLength); }

function readFile(name) {
  var p = path.join(scenarioDir, name);
  if (!fs.existsSync(p)) return null;
  return u8(fs.readFileSync(p));
}

var failures = 0;
var plaus = [];

function diffRange(a, b) {
  if (a.length !== b.length) return 'length ' + a.length + ' vs ' + b.length;
  for (var i = 0; i < a.length; i++) if (a[i] !== b[i]) return 'first diff at byte ' + i + ' (' + a[i] + ' vs ' + b[i] + ')';
  return null;
}

// Generic per-record round trip.
function checkRecords(label, data, recSize, decode, encode) {
  if (!data) { console.log('  skip ' + label + ' (missing)'); return; }
  if (data.length % recSize !== 0) {
    console.log('  FAIL ' + label + ': size ' + data.length + ' not a multiple of ' + recSize);
    failures++; return;
  }
  var n = data.length / recSize, bad = 0;
  for (var i = 0; i < n; i++) {
    var rec = data.subarray(i * recSize, (i + 1) * recSize);
    var out = encode(decode(rec));
    var d = diffRange(rec, out);
    if (d) { if (bad < 3) console.log('    rec ' + i + ': ' + d); bad++; }
  }
  if (bad) { console.log('  FAIL ' + label + ': ' + bad + '/' + n + ' records differ'); failures++; }
  else console.log('  ok   ' + label + ': ' + n + ' records identical');
}

function run() {
  console.log('Round-trip: ' + scenarioDir);

  // Outdoor + dungeon tile grids, doors, randlevels.
  checkRecords('Data LD  (outdoor field)', readFile('Data LD'), C.SIZE.field,
    C.decodeField, C.encodeField);
  checkRecords('Data DL  (dungeon field)', readFile('Data DL'), C.SIZE.field,
    C.decodeField, C.encodeField);
  checkRecords('Data DD  (outdoor doors)', readFile('Data DD'), C.SIZE.doors,
    C.decodeDoors, C.encodeDoors);
  checkRecords('Data DDD (dungeon doors)', readFile('Data DDD'), C.SIZE.doors,
    C.decodeDoors, C.encodeDoors);
  checkRecords('Data RD  (outdoor rand)', readFile('Data RD'), C.SIZE.randlevel,
    C.decodeRandlevel, C.encodeRandlevel);
  checkRecords('Data RDD (dungeon rand)', readFile('Data RDD'), C.SIZE.randlevel,
    C.decodeRandlevel, C.encodeRandlevel);
  checkRecords('Data MD2 (map metadata)', readFile('Data MD2'), C.SIZE.maps,
    C.decodeMaps, C.encodeMaps);

  // Plausibility sanity (offset gross-error detector), reported not asserted.
  var ld = readFile('Data LD'), rd = readFile('Data RD'), md2 = readFile('Data MD2');
  if (ld) console.log('\nplausibility: ' + (ld.length / C.SIZE.field) + ' outdoor levels');
  if (rd) {
    var rl0 = C.decodeRandlevel(rd.subarray(0, C.SIZE.randlevel));
    console.log('  level 0 landlook=' + rl0.landlook + ' isdark=' + rl0.isdark +
      ' uselos=' + rl0.uselos + ' (landlook should be 0..10)');
  }
  if (md2) {
    var m0 = C.decodeMaps(md2.subarray(0, C.SIZE.maps));
    console.log('  map 0 start=(' + m0.startx + ',' + m0.starty + ') level=' + m0.level +
      ' isdungeon=' + m0.isdungeon + ' note="' + m0.note + '"');
  }

  console.log('\n' + (failures ? 'FAILED (' + failures + ')' : 'PASS — all records round-trip byte-for-byte'));
  process.exit(failures ? 1 : 0);
}

run();
