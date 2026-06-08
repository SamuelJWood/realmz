/*
 * open_scenario.js — adds File > "Open Realmz Scenario…".
 *
 * Prompts for a scenario directory (e.g. ".../Scenarios/City of Bywater"),
 * figures out how many outdoor/dungeon levels it has from the field-file sizes,
 * writes a tiny ".realmz-level" pointer per level into <dir>/_tiled/, and opens
 * each one. The realmz.js map format handles the actual read/write.
 */

'use strict';

// Tiled provides no require(); the codec/scenario modules self-register on
// globalThis. Resolved lazily inside the action callback (runs after load).

function fileSize(path) {
  if (!File.exists(path)) return 0;
  var f = new BinaryFile(path, BinaryFile.ReadOnly);
  var n = f.size; f.close();
  return n;
}
function writePointer(path, obj) {
  var t = new TextFile(path, TextFile.WriteOnly);
  t.write(JSON.stringify(obj, null, 2));
  t.commit();
}

function openScenario(scenarioDir) {
  if (!scenarioDir) return;
  var C = (typeof RealmzCodec !== 'undefined') ? RealmzCodec : null;
  var S = (typeof RealmzScenario !== 'undefined') ? RealmzScenario : null;
  if (!C || !S) { tiled.alert('Realmz extension not fully loaded (codec/scenario missing).'); return; }
  // strip trailing slash
  scenarioDir = scenarioDir.replace(/[\\\/]+$/, '');
  var ptrDir = scenarioDir + '/_tiled';
  if (!File.exists(ptrDir)) File.makePath(ptrDir);

  var name = scenarioDir.split(/[\\\/]/).pop();
  var opened = 0;

  ['outdoor', 'dungeon'].forEach(function (kind) {
    var f = S.FILES[kind];
    var levels = S.countLevels(fileSize(scenarioDir + '/' + f.field));
    for (var i = 0; i < levels; i++) {
      var label = name + ' - ' + (kind === 'outdoor' ? 'Outdoor' : 'Dungeon') + ' ' + i;
      var ptr = ptrDir + '/' + label + '.realmz-level';
      writePointer(ptr, { scenarioDir: scenarioDir, kind: kind, index: i });
      tiled.open(ptr);
      opened++;
    }
  });

  if (opened === 0) {
    tiled.alert('No Realmz level data found in:\n' + scenarioDir +
      '\n(expected Data LD / Data DL).');
  } else {
    tiled.log('Realmz: opened ' + opened + ' level(s) from ' + scenarioDir);
  }
}

var action = tiled.registerAction('OpenRealmzScenario', function () {
  var dir = tiled.promptDirectory('', 'Choose a Realmz scenario folder');
  if (dir) openScenario(dir);
});
action.text = 'Open Realmz Scenario…';

tiled.extendMenu('File', [
  { separator: true },
  { action: 'OpenRealmzScenario' },
]);
