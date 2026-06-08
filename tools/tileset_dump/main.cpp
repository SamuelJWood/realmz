// tileset_dump — export Realmz graphics for the Tiled scenario editor.
//
// Reuses the engine's own decoders (the resource_file / ResourceDASM library
// that QuickDraw::GetPicture / GetCIcon call) so output is pixel-accurate.
//
// Outputs (under <output_dir>, default tools/tiled/realmz-extension/tilesets):
//   landlook_<n>.png       - landscape sheet for tileset n: PICT 300+n, 640x320
//                            = 20x10 = 200 tiles. Field value N maps to tile id
//                            N-1 (row-major, 20 cols); see centerpict.c.
//   icons/cicn_<id>.png    - every color icon (cicn): buildings, objects,
//                            monsters. Field values not in 1..200 are icon ids.
//   icons.json             - manifest { "<id>": {"w":W,"h":H}, ... }
//   landlook_basetile.json - { "<landlook>": <basetile_field_value>, ... }
//                            The engine draws basetile under every icon cell.
//   dungeon_overhead.png   - 4×6 grid of 32×32 tiles (scaled from 16×16 tiny
//                            tiles in PICT 302). Dungeon field values are
//                            bitmasks; bit N → tiny[N] → tile index N in this
//                            sheet. See threed.c:plotwall(), main.c:`tiny`.
//
// Usage: tileset_dump [<The Family Jewels.rsrc>] [<output_dir>]

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <phosg/Filesystem.hh>
#include <phosg/Image.hh>
#include <resource_file/IndexFormats/Formats.hh>
#include <resource_file/ResourceFile.hh>
#include <resource_file/ResourceTypes.hh>

using namespace ResourceDASM;

static const char* DEFAULT_RSRC =
    "base/Realmz/Data Files/The Family Jewels.rsrc";
static const char* DEFAULT_OUT = "tools/tiled/realmz-extension/tilesets";

static bool write_file(const std::string& path, const std::string& bytes) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    fprintf(stderr, "Cannot write %s\n", path.c_str());
    return false;
  }
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return true;
}

int main(int argc, char** argv) {
  std::string rsrc_path = argc > 1 ? argv[1] : DEFAULT_RSRC;
  std::string out_dir = argc > 2 ? argv[2] : DEFAULT_OUT;

  std::string data;
  try {
    data = phosg::load_file(rsrc_path);
  } catch (const std::exception& e) {
    fprintf(stderr, "Could not read resource file '%s': %s\n", rsrc_path.c_str(), e.what());
    return 1;
  }

  // The .rsrc files store the Mac resource fork as the file's data fork.
  ResourceFile rf = parse_resource_fork(data);

  // --- landscape tilesets: PICT 300+landlook (200 tiles each) ---
  // PICT 302 is a special 640×640 image: top half = landscape tiles,
  // bottom half = dungeon 3D textures + tiny overhead tiles (see below).
  int sheets = 0;
  bool have_pict302 = false;
  phosg::ImageRGBA8888N pict302_img;
  for (int landlook = 0; landlook <= 10; landlook++) {
    int16_t pict_id = static_cast<int16_t>(300 + landlook);
    if (!rf.resource_exists(RESOURCE_TYPE_PICT, pict_id)) continue;
    try {
      auto img = rf.decode_PICT(pict_id).image;
      if (img.get_width() == 0 || img.get_height() == 0) continue;
      std::string path = out_dir + "/landlook_" + std::to_string(landlook) + ".png";
      if (!write_file(path, img.serialize(phosg::ImageFormat::PNG))) return 1;
      printf("landscape %s (%zux%zu, %zu tiles)\n", path.c_str(),
          img.get_width(), img.get_height(), (img.get_width() / 32) * (img.get_height() / 32));
      if (landlook == 2 && img.get_height() >= 416) {
        pict302_img = std::move(img);
        have_pict302 = true;
      }
      sheets++;
    } catch (const std::exception& e) {
      fprintf(stderr, "Failed PICT %d: %s\n", pict_id, e.what());
    }
  }

  // --- dungeon overhead tileset from PICT 302 (tiny tiles) ---
  // GWorldInit.c allocates gthePixels as a 640×640 GWorld from PICT 302.
  // main.c sets up tiny[0..23]: a 4-col × 6-row grid of 16×16 tiles at
  // x=576..640, y=320..416. threed.c:plotwall() composites tiny[N] for each
  // set bit N in the dungeon field bitmask. We scale each 16×16 tile 2× to
  // 32×32 and arrange them in the same 4×6 layout → dungeon_overhead.png.
  if (have_pict302) {
    static const int TINY_COLS = 4, TINY_ROWS = 6;
    static const int TINY_SRC  = 16, TILE_DST = 32;
    static const int TINY_X0   = 576, TINY_Y0 = 320;

    phosg::ImageRGBA8888N out_img(TINY_COLS * TILE_DST, TINY_ROWS * TILE_DST);

    for (int tt = 0; tt < TINY_ROWS; tt++) {
      for (int t = 0; t < TINY_COLS; t++) {
        int src_x = TINY_X0 + t * TINY_SRC;
        int src_y = TINY_Y0 + tt * TINY_SRC;
        int dst_x = t * TILE_DST;
        int dst_y = tt * TILE_DST;
        // Nearest-neighbour 2× scale.
        for (int py = 0; py < TILE_DST; py++) {
          for (int px = 0; px < TILE_DST; px++) {
            uint32_t color = pict302_img.read(src_x + px / 2, src_y + py / 2);
            out_img.write(dst_x + px, dst_y + py, color);
          }
        }
      }
    }
    std::string dpath = out_dir + "/dungeon_overhead.png";
    if (!write_file(dpath, out_img.serialize(phosg::ImageFormat::PNG))) return 1;
    printf("dungeon overhead %s (%d×%d tiles = 24 tiny tiles)\n",
        dpath.c_str(), TINY_COLS, TINY_ROWS);
  } else {
    fprintf(stderr, "dungeon overhead: PICT 302 not found or too small, skipping\n");
  }

  // --- color icons (cicn): buildings, objects, monsters ---
  std::string icons_dir = out_dir + "/icons";
  std::error_code ec;
  std::filesystem::create_directories(icons_dir, ec);

  std::vector<int16_t> ids = rf.all_resources_of_type(RESOURCE_TYPE_cicn);
  std::string manifest = "{\n";
  int icons = 0;
  for (size_t k = 0; k < ids.size(); k++) {
    int16_t id = ids[k];
    try {
      auto img = rf.decode_cicn(id).image;
      if (img.get_width() == 0 || img.get_height() == 0) continue;
      std::string path = icons_dir + "/cicn_" + std::to_string(id) + ".png";
      if (!write_file(path, img.serialize(phosg::ImageFormat::PNG))) return 1;
      manifest += "  \"" + std::to_string(id) + "\": {\"w\": " +
          std::to_string(img.get_width()) + ", \"h\": " + std::to_string(img.get_height()) + "}";
      manifest += (icons + 1 < static_cast<int>(ids.size())) ? ",\n" : "\n";
      icons++;
    } catch (const std::exception& e) {
      fprintf(stderr, "Failed cicn %d: %s\n", id, e.what());
    }
  }
  // trim a possible trailing comma if the final entries failed to decode
  if (manifest.size() >= 2 && manifest[manifest.size() - 2] == ',') {
    manifest.erase(manifest.size() - 2, 1);
  }
  manifest += "}\n";
  if (!write_file(out_dir + "/icons.json", manifest)) return 1;

  // --- basetile per landlook: read from "Data * BD" files ---
  // centerpict.c draws basetile[lastpix] (a 1-based landscape tile value) under
  // every icon cell before overlaying the cicn image. The value is stored in the
  // tileset data file at byte offset sizeof(struct mapstats[402])/2 = 8040.
  // struct mapstats: 10+9+1 = 20 shorts = 40 bytes; 402 entries × 40 / 2 = 8040.
  static const size_t BASETILE_OFFSET = 8040;
  static const std::pair<int, const char*> BD_FILES[] = {
      {0, "Data P BD"},
      {3, "Data SUB BD"},
      {4, "Data Castle BD"},
      {5, "Data Desert BD"},
      {9, "Data Swamp BD"},
      {10, "Data Snow BD"},
  };

  // Derive the data files directory from the rsrc path.
  std::string data_files_dir = rsrc_path;
  auto slash = data_files_dir.find_last_of("/\\");
  if (slash != std::string::npos) data_files_dir = data_files_dir.substr(0, slash);

  std::string bt_manifest = "{\n";
  bool bt_first = true;
  int bt_count = 0;
  for (auto& [landlook_id, bd_name] : BD_FILES) {
    std::string bd_path = data_files_dir + "/" + bd_name;
    std::ifstream bd(bd_path, std::ios::binary);
    if (!bd) {
      fprintf(stderr, "basetile: skipping landlook %d (%s not found)\n", landlook_id, bd_path.c_str());
      continue;
    }
    bd.seekg(static_cast<std::streamoff>(BASETILE_OFFSET));
    uint8_t buf[2] = {};
    if (!bd.read(reinterpret_cast<char*>(buf), 2)) {
      fprintf(stderr, "basetile: failed to read from %s\n", bd_path.c_str());
      continue;
    }
    int16_t basetile = static_cast<int16_t>((static_cast<uint16_t>(buf[0]) << 8) | buf[1]);
    if (!bt_first) bt_manifest += ",\n";
    bt_manifest += "  \"" + std::to_string(landlook_id) + "\": " + std::to_string(basetile);
    bt_first = false;
    bt_count++;
    printf("basetile landlook %d: %d\n", landlook_id, basetile);
  }
  bt_manifest += "\n}\n";
  if (!write_file(out_dir + "/landlook_basetile.json", bt_manifest)) return 1;

  printf("Done: %d landscape sheet(s), %d icon(s), %d basetile(s) -> %s\n",
      sheets, icons, bt_count, out_dir.c_str());
  return (sheets > 0) ? 0 : 1;
}
