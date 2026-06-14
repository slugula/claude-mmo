#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

// =====================================================================
// AssetPack — transparent obfuscated asset container.
// =====================================================================
//
// Goal: keep model files out of casual reach in distributed builds. The
// production package ships a single opaque `assets.pak` (a packed,
// lightly-obfuscated archive built by tools/pack-assets.mjs) instead of a
// folder of named .glb files.
//
// This is obfuscation, NOT security: the client must deobfuscate to render,
// so a determined ripper can still recover geometry. It only stops casual
// "browse the folder and copy the model" snooping.
//
// Transparency: every model load funnels through GltfLoader::loadGlb, which
// asks AssetPack for the bytes. If `assets.pak` sits next to the executable
// AND contains the requested key, the (deobfuscated) packed bytes are
// returned; otherwise AssetPack reads the loose file from disk. So local dev
// and the level editor — which ship no .pak — keep using loose files with
// zero behaviour change and full hot-reload.
namespace assets {

// Read the bytes for an asset. `path` is normally the absolute path produced
// by the host's resolveFromExe(); AssetPack derives the pack key by taking it
// relative to the executable directory (e.g. "assets/models/Tree.glb").
// Returns std::nullopt if the asset is in neither the pack nor on disk.
std::optional<std::vector<unsigned char>> loadBytes(const std::filesystem::path& path);

// True if the asset can be loaded: present in the pack (by key) OR on disk.
// Use this instead of std::filesystem::exists() before loading models, so
// packed builds (where the loose file is absent) still find the asset.
bool exists(const std::filesystem::path& path);

// True once an assets.pak has been located and loaded (production builds).
bool packLoaded();

}  // namespace assets
