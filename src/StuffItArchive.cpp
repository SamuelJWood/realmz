#include "StuffItArchive.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <vector>

#include <phosg/Strings.hh>

static phosg::PrefixedLogger sit_log("[StuffIt] ");

namespace stuffit {
namespace {

// ---- Predefined static Huffman code-length tables (method 13, modes 1-5) ----
// Transcribed from the StuffIt 13 format description in The Unarchiver
// (XADMaster, XADStuffIt13Handle.m).
#include "StuffItTables.inc"

static const int* const kFirstCodeLengths[5] = {kFirst1, kFirst2, kFirst3, kFirst4, kFirst5};
static const int* const kSecondCodeLengths[5] = {kSecond1, kSecond2, kSecond3, kSecond4, kSecond5};
static const int* const kOffsetCodeLengths[5] = {kOffset1, kOffset2, kOffset3, kOffset4, kOffset5};
static const int kOffsetCodeSize[5] = {11, 13, 14, 11, 11};

// Metacode used to encode the dynamic Huffman tables (mode 0).
static const int kMetaCodes[37] = {
    0x5d8, 0x058, 0x040, 0x0c0, 0x000, 0x078, 0x02b, 0x014,
    0x00c, 0x01c, 0x01b, 0x00b, 0x010, 0x020, 0x038, 0x018,
    0x0d8, 0xbd8, 0x180, 0x680, 0x380, 0xf80, 0x780, 0x480,
    0x080, 0x280, 0x3d8, 0xfd8, 0x7d8, 0x9d8, 0x1d8, 0x004,
    0x001, 0x002, 0x007, 0x003, 0x008};
static const int kMetaCodeLengths[37] = {
    11, 8, 8, 8, 8, 7, 6, 5, 5, 5, 5, 6, 5, 6, 7, 7, 9, 12, 10,
    11, 11, 12, 12, 11, 11, 11, 12, 12, 12, 12, 12, 5, 2, 2, 3, 4, 5};

// Reads bits least-significant-bit-first within each byte, bytes in order
// (matching XAD's CSInputNext*LE family).
class BitReader {
public:
  BitReader(const uint8_t* data, size_t len) : data_(data), len_(len) {}

  uint8_t next_byte() {
    if (pos_ >= len_) throw std::runtime_error("StuffIt: unexpected end of data");
    return data_[pos_++];
  }

  int next_bit() {
    if (numbits_ == 0) {
      cur_ = next_byte();
      numbits_ = 8;
    }
    int b = cur_ & 1;
    cur_ >>= 1;
    numbits_--;
    return b;
  }

  // Returns n bits as an integer; the first bit read is the least significant.
  uint32_t next_bits(int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v |= static_cast<uint32_t>(next_bit()) << i;
    return v;
  }

private:
  const uint8_t* data_;
  size_t len_;
  size_t pos_ = 0;
  uint8_t cur_ = 0;
  int numbits_ = 0;
};

// Prefix (Huffman) code stored as a binary tree. Codes are inserted
// most-significant-bit-first; decoding consumes one stream bit per tree level,
// where the first stream bit selects the most significant code bit.
class PrefixCode {
public:
  PrefixCode() { nodes_.push_back(Node{}); }

  static uint32_t reverse_n(uint32_t val, int length) {
    uint32_t r = 0;
    for (int i = 0; i < length; i++) r = (r << 1) | ((val >> i) & 1);
    return r;
  }

  void insert_high_bit_first(int value, uint32_t code, int length) {
    int node = 0;
    for (int bitpos = length - 1; bitpos >= 0; bitpos--) {
      int bit = (code >> bitpos) & 1;
      if (nodes_[node].branch[bit] < 0) {
        nodes_.push_back(Node{});
        nodes_[node].branch[bit] = static_cast<int>(nodes_.size()) - 1;
      }
      node = nodes_[node].branch[bit];
    }
    nodes_[node].leaf = value;
  }

  void insert_low_bit_first(int value, uint32_t code, int length) {
    insert_high_bit_first(value, reverse_n(code, length), length);
  }

  // Build a canonical code where the shortest codes are all-zero bits.
  static PrefixCode from_lengths(const int* lengths, int numsymbols, int maxlength = 32) {
    PrefixCode c;
    uint32_t code = 0;
    for (int length = 1; length <= maxlength; length++) {
      for (int i = 0; i < numsymbols; i++) {
        if (lengths[i] != length) continue;
        c.insert_high_bit_first(i, code, length);
        code++;
      }
      code <<= 1;
    }
    return c;
  }

  int decode(BitReader& br) const {
    int node = 0;
    while (nodes_[node].leaf < 0) {
      int next = nodes_[node].branch[br.next_bit()];
      if (next < 0) throw std::runtime_error("StuffIt: invalid prefix code");
      node = next;
    }
    return nodes_[node].leaf;
  }

private:
  struct Node {
    int branch[2] = {-1, -1};
    int leaf = -1;
  };
  std::vector<Node> nodes_;
};

PrefixCode build_metacode() {
  PrefixCode c;
  for (int i = 0; i < 37; i++) c.insert_low_bit_first(i, kMetaCodes[i], kMetaCodeLengths[i]);
  return c;
}

// Parse one dynamically-encoded Huffman table of `numcodes` symbols.
PrefixCode parse_dynamic_code(BitReader& br, int numcodes, const PrefixCode& metacode) {
  std::vector<int> lengths(numcodes, 0);
  int length = 0;
  int i = 0;
  while (i < numcodes) {
    int val = metacode.decode(br);
    switch (val) {
      case 31: length = -1; lengths[i++] = length; break;
      case 32: length++; lengths[i++] = length; break;
      case 33: length--; lengths[i++] = length; break;
      case 34:
        if (br.next_bit()) lengths[i++] = length;
        if (i < numcodes) lengths[i++] = length;
        break;
      case 35: {
        int n = static_cast<int>(br.next_bits(3)) + 2;
        while (n-- > 0 && i < numcodes) lengths[i++] = length;
        if (i < numcodes) lengths[i++] = length;
        break;
      }
      case 36: {
        int n = static_cast<int>(br.next_bits(6)) + 10;
        while (n-- > 0 && i < numcodes) lengths[i++] = length;
        if (i < numcodes) lengths[i++] = length;
        break;
      }
      default: length = val + 1; lengths[i++] = length; break;
    }
  }
  for (auto& l : lengths)
    if (l < 0) l = 0; // negative => symbol has no code
  return PrefixCode::from_lengths(lengths.data(), numcodes);
}

// Decompress a method-13 stream to exactly `outlen` bytes.
std::vector<uint8_t> decompress13(const uint8_t* comp, size_t complen, size_t outlen) {
  BitReader br(comp, complen);
  int val = br.next_byte();
  int mode = val >> 4;

  PrefixCode firstcode, secondcode, offsetcode;
  if (mode == 0) {
    PrefixCode meta = build_metacode();
    firstcode = parse_dynamic_code(br, 321, meta);
    if (val & 0x08)
      secondcode = firstcode;
    else
      secondcode = parse_dynamic_code(br, 321, meta);
    offsetcode = parse_dynamic_code(br, (val & 0x07) + 10, meta);
  } else if (mode < 6) {
    int t = mode - 1;
    firstcode = PrefixCode::from_lengths(kFirstCodeLengths[t], 321);
    secondcode = PrefixCode::from_lengths(kSecondCodeLengths[t], 321);
    offsetcode = PrefixCode::from_lengths(kOffsetCodeLengths[t], kOffsetCodeSize[t]);
  } else {
    throw std::runtime_error("StuffIt: unsupported method-13 mode");
  }

  std::vector<uint8_t> out;
  out.reserve(outlen);
  std::vector<uint8_t> window(65536, 0);
  const int mask = 0xffff;
  int pos = 0;
  const PrefixCode* currcode = &firstcode;
  int matchlen = 0;
  int matchoff = 0;

  while (out.size() < outlen) {
    if (matchlen == 0) {
      int v = currcode->decode(br);
      if (v < 0x100) {
        currcode = &firstcode;
        window[pos & mask] = static_cast<uint8_t>(v);
        out.push_back(static_cast<uint8_t>(v));
        pos++;
        continue;
      }
      currcode = &secondcode;
      int length;
      if (v < 0x13e)
        length = v - 0x100 + 3;
      else if (v == 0x13e)
        length = static_cast<int>(br.next_bits(10)) + 65;
      else if (v == 0x13f)
        length = static_cast<int>(br.next_bits(15)) + 65;
      else
        break; // end marker

      int bl = offsetcode.decode(br);
      int offset;
      if (bl == 0)
        offset = 1;
      else if (bl == 1)
        offset = 2;
      else
        offset = (1 << (bl - 1)) + static_cast<int>(br.next_bits(bl - 1)) + 1;

      matchlen = length;
      matchoff = pos - offset;
    }
    matchlen--;
    uint8_t byte = window[matchoff++ & mask];
    window[pos & mask] = byte;
    out.push_back(byte);
    pos++;
  }
  return out;
}

// ---- Archive container parsing ----

uint32_t read_be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

constexpr size_t kEntryHeaderSize = 112;

std::string sanitize_name(const std::string& name) {
  std::string out;
  out.reserve(name.size());
  for (unsigned char c : name) {
    if (c >= 0x20 && c != '/' && c != '\\' && c != ':') out.push_back(static_cast<char>(c));
  }
  return out;
}

std::vector<uint8_t> decode_fork(int method, const uint8_t* comp, size_t complen, size_t outlen) {
  if (method == 0) return std::vector<uint8_t>(comp, comp + std::min(complen, outlen));
  if (method == 13) return decompress13(comp, complen, outlen);
  throw std::runtime_error("StuffIt: unsupported fork compression method " + std::to_string(method));
}

} // namespace

std::string root_folder_name(const std::string& sit_host_path) {
  try {
    std::string data = phosg::load_file(sit_host_path);
    if (data.size() < 22 || data.compare(0, 4, "SIT!") != 0) return "";
    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.data());
    size_t off = 22;
    if (off + kEntryHeaderSize > data.size()) return "";
    int rm = d[off], dm = d[off + 1], fnlen = d[off + 2];
    if (dm == 32 || rm == 32) {
      return sanitize_name(std::string(reinterpret_cast<const char*>(d + off + 3), std::min(fnlen, 63)));
    }
  } catch (const std::exception&) {
  }
  return "";
}

bool extract(const std::string& sit_host_path, const std::string& out_parent_dir) {
  std::string data;
  try {
    data = phosg::load_file(sit_host_path);
  } catch (const std::exception& e) {
    sit_log.warning_f("Cannot read archive {}: {}", sit_host_path, e.what());
    return false;
  }
  if (data.size() < 22 || data.compare(0, 4, "SIT!") != 0) {
    sit_log.warning_f("Not a StuffIt archive: {}", sit_host_path);
    return false;
  }

  const uint8_t* d = reinterpret_cast<const uint8_t*>(data.data());
  size_t off = 22;
  std::vector<std::filesystem::path> stack{std::filesystem::path(out_parent_dir)};

  try {
    while (off + kEntryHeaderSize <= data.size()) {
      int rm = d[off];
      int dm = d[off + 1];
      int fnlen = d[off + 2];
      std::string name = sanitize_name(
          std::string(reinterpret_cast<const char*>(d + off + 3), std::min(fnlen, 63)));
      uint32_t rlen = read_be32(d + off + 84);
      uint32_t dlen = read_be32(d + off + 88);
      uint32_t rclen = read_be32(d + off + 92);
      uint32_t dclen = read_be32(d + off + 96);

      if (dm == 32 || rm == 32) { // folder start
        std::filesystem::path dir = stack.back() / name;
        std::filesystem::create_directories(dir);
        stack.push_back(dir);
        off += kEntryHeaderSize;
        continue;
      }
      if (dm == 33 || rm == 33) { // folder end
        if (stack.size() > 1) stack.pop_back();
        off += kEntryHeaderSize;
        continue;
      }

      size_t data_start = off + kEntryHeaderSize;
      if (data_start + rclen + dclen > data.size())
        throw std::runtime_error("StuffIt: entry data out of bounds");
      const uint8_t* rdata = d + data_start;
      const uint8_t* ddata = d + data_start + rclen;

      // Some scenario archives store resource forks as ordinary sidecar data
      // files with a ".rsf" extension (e.g. "Scenario.rsf") rather than as true
      // Mac resource forks. ".rsf" is the same raw resource-fork format as the
      // ".rsrc" sidecar convention used here, so normalize the name on the way
      // out so the engine finds it.
      std::string data_name = name;
      if (data_name.size() > 4 && data_name.compare(data_name.size() - 4, 4, ".rsf") == 0) {
        data_name = data_name.substr(0, data_name.size() - 4) + ".rsrc";
      }

      std::filesystem::path base = stack.back() / name;
      if (dlen > 0) {
        std::vector<uint8_t> dec = decode_fork(dm, ddata, dclen, dlen);
        std::string p = (stack.back() / data_name).string();
        FILE* f = fopen(p.c_str(), "wb");
        if (!f) throw std::runtime_error("StuffIt: cannot write " + p);
        if (!dec.empty()) fwrite(dec.data(), 1, dec.size(), f);
        fclose(f);
      }
      if (rlen > 0) {
        std::vector<uint8_t> dec = decode_fork(rm, rdata, rclen, rlen);
        std::string p = base.string() + ".rsrc";
        FILE* f = fopen(p.c_str(), "wb");
        if (!f) throw std::runtime_error("StuffIt: cannot write " + p);
        if (!dec.empty()) fwrite(dec.data(), 1, dec.size(), f);
        fclose(f);
      }

      off += kEntryHeaderSize + rclen + dclen;
    }
  } catch (const std::exception& e) {
    sit_log.warning_f("Failed to extract {}: {}", sit_host_path, e.what());
    return false;
  }

  return true;
}

} // namespace stuffit
