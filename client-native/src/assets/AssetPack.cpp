#include "assets/AssetPack.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace assets {
namespace {

// ---- Obfuscation -----------------------------------------------------------
// Rolling XOR against a fixed 64-byte key, with a per-entry phase derived from
// the asset key so identical byte runs across files don't share a keystream.
// The packer (tools/pack-assets.mjs) MUST keep this key + scheme byte-identical.
constexpr unsigned char kKey[64] = {
    0x7a, 0x1f, 0xc3, 0x9b, 0x46, 0xe2, 0x58, 0x0d, 0xb7, 0x31, 0x6c, 0xa9,
    0xf4, 0x12, 0x8e, 0x5d, 0x24, 0xd0, 0x6f, 0x93, 0xab, 0x07, 0x55, 0xee,
    0x19, 0xc8, 0x3a, 0x71, 0x9d, 0x42, 0xb0, 0x6b, 0xf7, 0x2c, 0x88, 0x51,
    0x0e, 0xa3, 0xd6, 0x64, 0x1a, 0xbf, 0x77, 0x35, 0xe9, 0x40, 0x9c, 0x28,
    0x83, 0x5f, 0xc1, 0x16, 0x7e, 0xaa, 0xd3, 0x68, 0x04, 0xb9, 0x47, 0xf1,
    0x2a, 0x95, 0x60, 0xdc};

uint32_t fnv1a(const std::string& s) {
  uint32_t h = 2166136261u;
  for (char c : s) {
    h ^= static_cast<unsigned char>(c);
    h *= 16777619u;
  }
  return h;
}

void deobfuscate(std::vector<unsigned char>& buf, const std::string& key) {
  const int phase = static_cast<int>(fnv1a(key) & 63u);
  for (std::size_t i = 0; i < buf.size(); ++i)
    buf[i] ^= kKey[(static_cast<int>(i) + phase) & 63];
}

// ---- Pack model ------------------------------------------------------------
struct Entry {
  uint64_t offset = 0;
  uint64_t size   = 0;
};

struct Pack {
  bool                                   loaded = false;
  std::filesystem::path                  file;            // path to assets.pak
  std::unordered_map<std::string, Entry> entries;         // key (lowercased) → blob
  std::filesystem::path                  exeDir;
};

std::once_flag g_once;
Pack           g_pack;

std::string toKey(std::string s) {
  std::replace(s.begin(), s.end(), '\\', '/');
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::filesystem::path executableDir() {
#ifdef _WIN32
  char buf[MAX_PATH] = {0};
  const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
  if (n > 0 && n < MAX_PATH)
    return std::filesystem::path(buf).parent_path();
#endif
  return std::filesystem::current_path();
}

template <typename T>
bool readLE(std::ifstream& f, T& out) {
  f.read(reinterpret_cast<char*>(&out), sizeof(T));
  return static_cast<bool>(f);
}

void loadOnce() {
  g_pack.exeDir = executableDir();
  const std::filesystem::path pakPath = g_pack.exeDir / "assets.pak";
  std::error_code ec;
  if (!std::filesystem::exists(pakPath, ec)) return;  // dev/editor: loose files

  std::ifstream f(pakPath, std::ios::binary);
  if (!f) return;

  char magic[4] = {0};
  f.read(magic, 4);
  if (std::memcmp(magic, "LPAK", 4) != 0) {
    std::fprintf(stderr, "[AssetPack] bad magic in %s\n", pakPath.string().c_str());
    return;
  }
  uint32_t version = 0, count = 0;
  if (!readLE(f, version) || !readLE(f, count)) return;

  for (uint32_t i = 0; i < count; ++i) {
    uint32_t keyLen = 0;
    if (!readLE(f, keyLen) || keyLen == 0 || keyLen > 4096) return;
    std::string key(keyLen, '\0');
    f.read(key.data(), keyLen);
    Entry e;
    if (!readLE(f, e.offset) || !readLE(f, e.size)) return;
    if (!f) return;
    g_pack.entries[toKey(key)] = e;
  }

  g_pack.file   = pakPath;
  g_pack.loaded = true;
  std::fprintf(stderr, "[AssetPack] loaded %s (%u entries)\n",
               pakPath.string().c_str(), count);
}

}  // namespace

bool packLoaded() {
  std::call_once(g_once, loadOnce);
  return g_pack.loaded;
}

std::optional<std::vector<unsigned char>> loadBytes(const std::filesystem::path& path) {
  std::call_once(g_once, loadOnce);

  // Derive the pack key: path relative to the executable directory.
  if (g_pack.loaded) {
    std::error_code ec;
    std::filesystem::path rel = std::filesystem::relative(path, g_pack.exeDir, ec);
    const std::string key = toKey(ec ? path.generic_string() : rel.generic_string());
    const auto it = g_pack.entries.find(key);
    if (it != g_pack.entries.end()) {
      std::ifstream f(g_pack.file, std::ios::binary);
      if (f) {
        std::vector<unsigned char> buf(it->second.size);
        f.seekg(static_cast<std::streamoff>(it->second.offset));
        f.read(reinterpret_cast<char*>(buf.data()),
               static_cast<std::streamsize>(buf.size()));
        if (f) {
          deobfuscate(buf, key);
          return buf;
        }
      }
      // Pack hit but read failed — fall through to loose-file attempt.
    }
  }

  // Loose file on disk (dev, editor, or assets not in the pack).
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) return std::nullopt;
  const std::streamsize n = f.tellg();
  if (n < 0) return std::nullopt;
  std::vector<unsigned char> buf(static_cast<std::size_t>(n));
  f.seekg(0);
  f.read(reinterpret_cast<char*>(buf.data()), n);
  if (!f) return std::nullopt;
  return buf;
}

}  // namespace assets
