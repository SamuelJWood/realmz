# Realmz scenario editor for Tiled

Edit Realmz scenario **maps, per-tile events, random-event areas, and per-level
metadata** in the [Tiled map editor](https://www.mapeditor.org/), writing changes
straight back into the scenario's own `Data *` binaries.

## Install

1. Install Tiled (1.9 or newer recommended).
2. In Tiled: **Edit → Preferences → Plugins → "Open" (the extensions folder)**.
3. Copy this whole `realmz-extension` folder into that extensions folder.
4. Restart Tiled. You should now have **File → "Open Realmz Scenario…"**.

## Generate tilesets (one-time)

Pre-generated tileset PNGs are checked in under `tilesets/`. To regenerate them
(e.g. from a different `The Family Jewels.rsrc`), build the `tileset_dump` helper,
which reuses the engine's own PICT decoder (the `resource_file` library):

```
# from the repo root
cmake -S . -B build -DREALMZ_BUILD_TILESET_DUMP=ON
cmake --build build --target tileset_dump
./build/tools/tileset_dump/tileset_dump            # uses default paths
# or: tileset_dump "<path/The Family Jewels.rsrc>" "<output_dir>"
```

It exports landscape sheets `tilesets/landlook_<n>.png` (PICT 300+landlook, 200
tiles, 640px/20-wide so field value `N` maps to tile id `N-1`) **and** every
color icon to `tilesets/icons/cicn_<id>.png` plus an `icons.json` manifest. The
icons are the buildings/objects/monsters the engine draws via `GetCIcon`.

## Use

1. **File → Open Realmz Scenario…** and pick a scenario folder
   (e.g. `base/Realmz/Scenarios/City of Bywater`).
2. Each level opens as its own document: `<scenario> - Outdoor <n>` /
   `- Dungeon <n>`. (Tiny `.realmz-level` pointer files are written into a
   `_tiled/` subfolder; you can reopen a level later by opening its pointer.)
3. Edit and **Save** (Ctrl+S). On first save of a file this session, a `.bak`
   copy of each touched binary is made next to it.

### What each layer is

- **Tiles** — the 90×90 landscape grid (field values 1–200). Paint from the
  `landlook_<n>` tileset.
- **Icons** — buildings, structures, trees, and map monsters. These are cicn
  color icons (field values that are negative, or >200 after the note/path/secret
  flags are stripped), shown as tile-objects from the `Realmz Icons` collection.
  Move/delete/add them; each carries `iconId` (the cicn resource id) and `raw`
  (the original field value) so unchanged icons round-trip exactly. A new/moved
  icon writes its bare `iconId`.
- **Events** — one object per actionable door (`struct door`). Properties:
  `slot`, `percent`, `landid/landx/landy`, and `code1..code8` / `id1..id8`
  (the 8-slot action script). Opcode numbers are listed in `opcodes.json`; the
  object name shows the first action's label. Negative `code` = GOSUB to the
  door whose id == −code. Move an object to change the tile its trigger fires on;
  delete it to clear that door's actions; add a Rectangle to create a new event
  (it claims a free door slot).
- **Random Areas** — `randlevel.randrect[20]` rectangles. Properties: `percent`,
  `battle_min/max`, `sound`, `text`, `option`, `only`, `randdoor*`.
- **Journal Maps** (outdoor level 0 only) — the clue/treasure maps from
  `Data MD2`, shown to the player via the "Give/display map" action. Edit `note`,
  `target_level`, `startx/starty`, etc. Saved when you save outdoor level 0.
- **Map custom properties** — `landlook` (tileset id), `isdark`, `uselos`.

### Apply changes in-game

The engine builds its working maps (`:Data Files:CL`/`CD`) from the scenario at
new-game time, so just start a **new game** in the edited scenario to see changes.

## Known limitations (v1)

- Landscape tiles (`1..200`) are on the Tiles layer; everything else (buildings,
  objects, monsters) is a cicn icon on the Icons layer. Note/path/secret overlay
  flags on a landscape tile are preserved on save but not visualized. An icon
  whose cicn id is scenario-specific (in `Scenario.rsrc`, not exported) shows as
  a blank 32×32 marker but is still editable and preserved.
- The "ground" the engine draws under an icon is not shown in the Tiles layer
  (icon cells look empty there, with the icon object on top). Painting a new
  landscape tile under an icon won't take effect unless you also remove the icon.
- Opcode operands are edited as raw numbers. The label table is read-only.
- Dialog text (`Data SD2/DES/OD`) is edited separately via
  `tools/scenario_text.py`, not here.

## Developer tests

The binary codec and the event/area/tile reconcile logic are pure JS and tested
under Node (no Tiled needed):

```
node test/roundtrip.js     # every record round-trips byte-for-byte
node test/scenario.js      # event/area extraction is lossless + summary
node test/reconcile.js     # add / edit / delete + field reconcile
```

`realmz.js` / `open_scenario.js` are the only Tiled-API code; everything they
rely on lives in `realmz_codec.js` and `realmz_scenario.js`.
