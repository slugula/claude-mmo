// World View — multi-chunk overworld manifest editing (world.json).
//
// Each cell of the world grid holds one chunkSize×chunkSize map file. The
// server assembles assigned chunks into one seamless global world; this window
// is where chunks are assigned, moved, erased, and opened, and where the world
// spawn is set. Implemented as EditorApp methods in a separate TU to keep
// EditorApp.cpp from growing further.

#include "editor/EditorApp.hpp"
#include "shared/SharedTypesJson.hpp"
#include "world/TerrainBuilder.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>

#ifndef NOMINMAX
#define NOMINMAX       // keep windows.h from clobbering std::min/std::max
#endif
#include <windows.h>   // GetModuleFileNameW for the exe-relative maps dir
#include <commdlg.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>

namespace editor {

namespace {

constexpr float kCellZoomMin = 32.0f;
constexpr float kCellZoomMax = 256.0f;

// Repo-canonical maps directory (public/maps), resolved relative to the exe:
// Release/ → build/ → client-native/ → root/.
std::filesystem::path canonicalMapsDir() {
  wchar_t exePath[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, exePath, MAX_PATH);
  std::filesystem::path dir = std::filesystem::path(exePath).parent_path()
                              / L"../../../public/maps";
  std::error_code ec;
  dir = std::filesystem::canonical(dir, ec);
  if (ec) return {};
  return dir;
}

bool parseHex(const char* s, float& r, float& g, float& b) {
  if (!s || s[0] != '#' || std::strlen(s) < 7) return false;
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  };
  r = static_cast<float>(nib(s[1]) * 16 + nib(s[2])) / 255.0f;
  g = static_cast<float>(nib(s[3]) * 16 + nib(s[4])) / 255.0f;
  b = static_cast<float>(nib(s[5]) * 16 + nib(s[6])) / 255.0f;
  return true;
}

}  // namespace

std::filesystem::path EditorApp::worldDir() const {
  if (!worldManifestPath_.empty())
    return std::filesystem::path(worldManifestPath_).parent_path();
  return canonicalMapsDir();
}

shared::WorldChunkRef* EditorApp::worldCellAt(int cx, int cy) {
  for (auto& c : worldManifest_.chunks)
    if (c.cx == cx && c.cy == cy) return &c;
  return nullptr;
}

void EditorApp::worldNewManifest() {
  worldManifest_ = {};
  worldManifestPath_.clear();
  worldDirty_ = true;
  worldDestroyThumbs();
  neighbors_.clear();
}

void EditorApp::worldOpenManifest() {
  const std::wstring path = winOpenDialog();
  if (path.empty()) return;
  shared::WorldManifest loaded;
  if (!shared::loadWorldManifest(std::filesystem::path(path), loaded)) return;
  worldManifest_     = std::move(loaded);
  worldManifestPath_ = std::filesystem::path(path).string();
  worldDirty_ = false;
  worldDestroyThumbs();
  worldRefreshNeighbors();
}

void EditorApp::worldSaveManifest() {
  if (worldManifestPath_.empty()) {
    // Default the save dialog into the canonical maps dir as world.json.
    const std::wstring path = winSaveDialog();
    if (path.empty()) return;
    worldManifestPath_ = std::filesystem::path(path).string();
  }
  if (shared::saveWorldManifest(std::filesystem::path(worldManifestPath_), worldManifest_))
    worldDirty_ = false;
}

void EditorApp::worldAssignCell(int cx, int cy, const std::string& mapFile) {
  if (auto* existing = worldCellAt(cx, cy)) {
    existing->mapFile = mapFile;
  } else {
    shared::WorldChunkRef ref;
    ref.cx = cx; ref.cy = cy; ref.mapFile = mapFile;
    ref.name = std::filesystem::path(mapFile).stem().string();
    worldManifest_.chunks.push_back(std::move(ref));
  }
  worldDirty_ = true;
  worldRefreshNeighbors();
}

void EditorApp::worldEraseCell(int cx, int cy) {
  auto& cs = worldManifest_.chunks;
  cs.erase(std::remove_if(cs.begin(), cs.end(),
           [&](const shared::WorldChunkRef& c) { return c.cx == cx && c.cy == cy; }),
           cs.end());
  worldDirty_ = true;
  worldRefreshNeighbors();
}

void EditorApp::worldOpenChunk(int cx, int cy) {
  const auto* cell = worldCellAt(cx, cy);
  if (!cell) return;
  const auto path = worldDir() / cell->mapFile;
  // openRecentFile loads + rebuilds GL + recents + title + neighbor ghosts.
  openRecentFile(path.string());
}

void EditorApp::worldDestroyThumbs() {
  for (auto& [file, tex] : worldThumbs_)
    if (tex != 0) glDeleteTextures(1, &tex);
  worldThumbs_.clear();
}

// 64×64 ground-colour thumbnail, lazily rasterised per mapFile and cached.
// Failure (missing/unreadable map) caches 0 so we don't retry every frame.
GLuint EditorApp::worldThumbnail(const std::string& mapFile) {
  if (auto it = worldThumbs_.find(mapFile); it != worldThumbs_.end())
    return it->second;

  GLuint tex = 0;
  shared::WorldMapFile m;
  if (shared::loadWorldMap(worldDir() / mapFile, m) && m.width > 0 && m.height > 0) {
    const int W = m.width, H = m.height;
    std::vector<uint32_t> px(static_cast<std::size_t>(W) * H, 0xFF000000u);
    for (int ty = 0; ty < H; ++ty) {
      for (int tx = 0; tx < W; ++tx) {
        float r = 0.49f, g = 0.78f, b = 0.31f;
        parseHex(m.tiles[ty][tx].groundColor.c_str(), r, g, b);
        if (!m.tiles[ty][tx].obstacle.empty() && m.tiles[ty][tx].obstacle != "none") {
          r *= 0.55f; g *= 0.55f; b *= 0.55f;
        }
        // Horizontal flip to match the 2D grid / minimap orientation
        // (east = screen-left).
        px[static_cast<std::size_t>(ty) * W + (W - 1 - tx)] =
            0xFF000000u |
            (static_cast<uint32_t>(b * 255.0f) << 16) |
            (static_cast<uint32_t>(g * 255.0f) << 8)  |
             static_cast<uint32_t>(r * 255.0f);
      }
    }
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
  }
  worldThumbs_[mapFile] = tex;
  return tex;
}

// ---- Neighbor-edge ghost preview -------------------------------------------
// When the currently open map file is assigned to a manifest cell, load its
// up-to-8 neighbors read-only and bake each into a dimmed terrain mesh offset
// to its world position. Rendered after the main terrain in render3DViewport;
// drawn as border tiles in the 2D grid.

void EditorApp::worldRefreshNeighbors() {
  neighbors_.clear();
  if (!neighborPreviewEnabled_ || currentFilePath_.empty()) return;

  // Which cell (if any) is the open map assigned to? Compare canonical paths
  // so relative manifest entries match the absolute currentFilePath_.
  std::error_code ec;
  const auto current = std::filesystem::canonical(currentFilePath_, ec);
  if (ec) return;
  const shared::WorldChunkRef* self = nullptr;
  for (const auto& c : worldManifest_.chunks) {
    const auto p = std::filesystem::canonical(worldDir() / c.mapFile, ec);
    if (!ec && p == current) { self = &c; break; }
  }
  if (!self) return;

  const int S = worldManifest_.chunkSize;
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      if (dx == 0 && dy == 0) continue;
      const auto* cell = worldCellAt(self->cx + dx, self->cy + dy);
      if (!cell) continue;

      NeighborPreview np;
      np.dcx = dx; np.dcy = dy;
      if (!shared::loadWorldMap(worldDir() / cell->mapFile, np.map)) continue;
      if (np.map.width != S || np.map.height != S) {
        std::fprintf(stderr, "[worldView] neighbor %s is %dx%d, expected %d — skipping ghost\n",
                     cell->mapFile.c_str(), np.map.width, np.map.height, S);
        continue;
      }

      // Bake the world offset and a ghost dim into the mesh so it renders with
      // the unmodified terrain shader. Tile y maps to world Z = y (the mesh's
      // internal row flip cancels per-map), so the cell offset is simply
      // (+dx*S, 0, +dy*S).
      auto md = world::buildTerrainMesh(np.map);
      const float ox = static_cast<float>(dx * S);
      const float oz = static_cast<float>(dy * S);
      for (std::size_t i = 0; i + 2 < md.positions.size(); i += 3) {
        md.positions[i]     += ox;
        md.positions[i + 2] += oz;
      }
      for (std::size_t i = 0; i + 3 < md.colors.size(); i += 4) {
        md.colors[i]     = md.colors[i]     * 0.45f + 0.18f;
        md.colors[i + 1] = md.colors[i + 1] * 0.45f + 0.18f;
        md.colors[i + 2] = md.colors[i + 2] * 0.45f + 0.18f;
      }
      np.mesh.upload(md.positions, md.colors, md.triangleIndices, {}, md.normals);
      neighbors_.push_back(std::move(np));
    }
  }
}

// ---- World View window ------------------------------------------------------

void EditorApp::drawWorldView() {
  // One-time autoload of the canonical world.json so the grid is populated
  // the first time the window opens.
  if (!worldAutoloaded_) {
    worldAutoloaded_ = true;
    const auto p = canonicalMapsDir() / "world.json";
    std::error_code ec;
    if (std::filesystem::exists(p, ec)) {
      shared::WorldManifest loaded;
      if (shared::loadWorldManifest(p, loaded)) {
        worldManifest_     = std::move(loaded);
        worldManifestPath_ = p.string();
        worldDirty_ = false;
        worldRefreshNeighbors();
      }
    }
  }

  bool open = true;
  ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
  const std::string title = std::string("World View") +
      (worldDirty_ ? " *" : "") + "###worldview";
  if (!ImGui::Begin(title.c_str(), &open)) { ImGui::End(); if (!open) showWorldView_ = false; return; }
  if (!open) showWorldView_ = false;

  // ---- Toolbar row ----
  if (ImGui::Button("New World"))  worldNewManifest();
  ImGui::SameLine();
  if (ImGui::Button("Open World...")) worldOpenManifest();
  ImGui::SameLine();
  if (ImGui::Button("Save World")) worldSaveManifest();
  ImGui::SameLine();
  ImGui::Checkbox("Neighbor ghosts", &neighborPreviewEnabled_);
  if (ImGui::IsItemEdited()) worldRefreshNeighbors();
  ImGui::SameLine();
  ImGui::TextDisabled("chunk %dpx  spawn (%d,%d)  %d chunk(s)",
                      worldManifest_.chunkSize,
                      worldManifest_.spawn.x, worldManifest_.spawn.y,
                      static_cast<int>(worldManifest_.chunks.size()));

  // ---- Grid canvas ----
  const ImVec2 canvasPos  = ImGui::GetCursorScreenPos();
  const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
  ImDrawList* dl          = ImGui::GetWindowDrawList();
  const auto& io          = ImGui::GetIO();

  dl->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                    IM_COL32(24, 26, 30, 255));
  dl->PushClipRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);

  // Invisible button claims mouse interaction over the canvas.
  ImGui::InvisibleButton("##worldcanvas", ImVec2(std::max(canvasSize.x, 1.0f), std::max(canvasSize.y, 1.0f)));
  const bool hovered = ImGui::IsItemHovered();

  if (hovered) {
    if (io.MouseWheel != 0.0f)
      worldZoom_ = std::clamp(worldZoom_ * (io.MouseWheel > 0 ? 1.15f : 1.0f / 1.15f),
                              kCellZoomMin, kCellZoomMax);
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
      const auto d = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle, 0.0f);
      ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
      worldOffX_ += d.x; worldOffY_ += d.y;
    }
  }

  const float z  = worldZoom_;
  const float ox = canvasPos.x + worldOffX_ + 16.0f;
  const float oy = canvasPos.y + worldOffY_ + 16.0f;
  auto cellMin = [&](int cx, int cy) { return ImVec2(ox + cx * z, oy + cy * z); };

  // Hovered cell under the mouse (non-negative cells only — v1 world space).
  int hovCx = -1, hovCy = -1;
  if (hovered) {
    const float fx = (io.MousePos.x - ox) / z;
    const float fy = (io.MousePos.y - oy) / z;
    if (fx >= 0.0f && fy >= 0.0f) { hovCx = static_cast<int>(fx); hovCy = static_cast<int>(fy); }
  }

  // Visible grid extent: bounding box of assigned chunks plus margin, at least 4×4.
  int maxCx = 3, maxCy = 3;
  for (const auto& c : worldManifest_.chunks) {
    maxCx = std::max(maxCx, c.cx + 2);
    maxCy = std::max(maxCy, c.cy + 2);
  }

  const int S = worldManifest_.chunkSize;
  const int spawnCx = (S > 0) ? worldManifest_.spawn.x / S : 0;
  const int spawnCy = (S > 0) ? worldManifest_.spawn.y / S : 0;

  // Which cell is the currently open map (highlight)?
  int openCx = INT_MIN, openCy = INT_MIN;
  if (!currentFilePath_.empty()) {
    std::error_code ec;
    const auto current = std::filesystem::canonical(currentFilePath_, ec);
    if (!ec) {
      for (const auto& c : worldManifest_.chunks) {
        const auto p = std::filesystem::canonical(worldDir() / c.mapFile, ec);
        if (!ec && p == current) { openCx = c.cx; openCy = c.cy; break; }
      }
    }
  }

  for (int cy = 0; cy <= maxCy; ++cy) {
    for (int cx = 0; cx <= maxCx; ++cx) {
      const ImVec2 a = cellMin(cx, cy);
      const ImVec2 b = ImVec2(a.x + z - 2.0f, a.y + z - 2.0f);
      if (b.x < canvasPos.x || a.x > canvasPos.x + canvasSize.x ||
          b.y < canvasPos.y || a.y > canvasPos.y + canvasSize.y) continue;

      const auto* cell = worldCellAt(cx, cy);
      if (cell) {
        if (GLuint tex = worldThumbnail(cell->mapFile))
          dl->AddImage((ImTextureID)(uintptr_t)tex, a, b);
        else
          dl->AddRectFilled(a, b, IM_COL32(70, 50, 50, 255));
        const bool isOpen = (cx == openCx && cy == openCy);
        dl->AddRect(a, b, isOpen ? IM_COL32(255, 210, 60, 255) : IM_COL32(160, 160, 160, 180),
                    0.0f, 0, isOpen ? 2.5f : 1.0f);
        if (z >= 56.0f) {
          const std::string& label = cell->name.empty() ? cell->mapFile : cell->name;
          dl->AddText(ImVec2(a.x + 4, a.y + 3), IM_COL32(0, 0, 0, 200), label.c_str());
          dl->AddText(ImVec2(a.x + 3, a.y + 2), IM_COL32(255, 255, 255, 230), label.c_str());
        }
      } else {
        dl->AddRectFilled(a, b, IM_COL32(38, 41, 46, 255));
        dl->AddRect(a, b, IM_COL32(70, 74, 80, 255));
      }

      if (cx == spawnCx && cy == spawnCy && !worldManifest_.chunks.empty()) {
        // Gold spawn marker at the spawn's intra-cell position.
        const float sxF = (S > 0) ? static_cast<float>(worldManifest_.spawn.x % S) / S : 0.5f;
        const float syF = (S > 0) ? static_cast<float>(worldManifest_.spawn.y % S) / S : 0.5f;
        // Thumbnails are horizontally flipped (east = left) — mirror to match.
        const ImVec2 c(a.x + (1.0f - sxF) * (z - 2.0f), a.y + syF * (z - 2.0f));
        dl->AddCircleFilled(c, std::max(3.0f, z * 0.05f), IM_COL32(255, 200, 40, 255));
        dl->AddCircle(c, std::max(4.5f, z * 0.07f), IM_COL32(0, 0, 0, 200), 0, 1.5f);
      }

      if (cx == hovCx && cy == hovCy)
        dl->AddRect(a, b, IM_COL32(255, 255, 255, 200), 0.0f, 0, 2.0f);
    }
  }

  // Drag-move ghost line.
  if (worldDragCx_ != INT_MIN) {
    const ImVec2 a = cellMin(worldDragCx_, worldDragCy_);
    dl->AddRect(a, ImVec2(a.x + z - 2, a.y + z - 2), IM_COL32(80, 180, 255, 255), 0.0f, 0, 2.5f);
    dl->AddLine(ImVec2(a.x + z * 0.5f, a.y + z * 0.5f), io.MousePos, IM_COL32(80, 180, 255, 180), 2.0f);
  }

  dl->PopClipRect();

  // ---- Interactions ----
  if (hovered && hovCx >= 0) {
    // Left press on a filled cell starts a potential drag-move.
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && worldCellAt(hovCx, hovCy)) {
      worldDragCx_ = hovCx; worldDragCy_ = hovCy;
    }
    // Double-click opens the chunk for editing.
    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && worldCellAt(hovCx, hovCy)) {
      worldDragCx_ = INT_MIN;
      worldOpenChunk(hovCx, hovCy);
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
      ImGui::OpenPopup(worldCellAt(hovCx, hovCy) ? "##cellmenu" : "##assignmenu");
      worldDragCx_ = INT_MIN;
      // Stash the target cell for the popup.
      ImGui::GetStateStorage()->SetInt(ImGui::GetID("##popupCx"), hovCx);
      ImGui::GetStateStorage()->SetInt(ImGui::GetID("##popupCy"), hovCy);
    }
  }

  // Finish a drag-move on release.
  if (worldDragCx_ != INT_MIN && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    if (hovered && hovCx >= 0 && (hovCx != worldDragCx_ || hovCy != worldDragCy_)) {
      auto* from = worldCellAt(worldDragCx_, worldDragCy_);
      auto* to   = worldCellAt(hovCx, hovCy);
      if (from) {
        if (to) { // swap assignments
          std::swap(from->mapFile, to->mapFile);
          std::swap(from->name,    to->name);
        } else {  // move
          from->cx = hovCx; from->cy = hovCy;
        }
        worldDirty_ = true;
        worldRefreshNeighbors();
      }
    }
    worldDragCx_ = INT_MIN;
  }

  const int popCx = ImGui::GetStateStorage()->GetInt(ImGui::GetID("##popupCx"), -1);
  const int popCy = ImGui::GetStateStorage()->GetInt(ImGui::GetID("##popupCy"), -1);

  // ---- Context menu: filled cell ----
  if (ImGui::BeginPopup("##cellmenu")) {
    const auto* cell = worldCellAt(popCx, popCy);
    ImGui::TextDisabled("(%d,%d) %s", popCx, popCy, cell ? cell->mapFile.c_str() : "?");
    ImGui::Separator();
    if (ImGui::MenuItem("Open for editing")) worldOpenChunk(popCx, popCy);
    if (ImGui::MenuItem("Set world spawn here")) {
      const int S2 = worldManifest_.chunkSize;
      worldManifest_.spawn.x = popCx * S2 + S2 / 2;
      worldManifest_.spawn.y = popCy * S2 + S2 / 2;
      worldDirty_ = true;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Erase assignment")) worldEraseCell(popCx, popCy);
    ImGui::EndPopup();
  }

  // ---- Context menu: empty cell (assign) ----
  if (ImGui::BeginPopup("##assignmenu")) {
    ImGui::TextDisabled("Assign chunk to (%d,%d)", popCx, popCy);
    ImGui::Separator();

    // Assign the currently open map (when saved and inside the world dir).
    {
      bool canAssignCurrent = false;
      std::string rel;
      if (!currentFilePath_.empty()) {
        std::error_code ec;
        const auto r = std::filesystem::relative(currentFilePath_, worldDir(), ec);
        if (!ec && !r.empty() && r.native()[0] != L'.') {
          rel = r.generic_string();
          canAssignCurrent = true;
        }
      }
      if (ImGui::MenuItem("Assign current map", nullptr, false, canAssignCurrent))
        worldAssignCell(popCx, popCy, rel);
    }

    if (ImGui::MenuItem("New blank chunk...")) {
      // Create chunk_<cx>_<cy>.json (flat walkable grass) and assign it.
      const int S2 = worldManifest_.chunkSize;
      char nameBuf[64];
      std::snprintf(nameBuf, sizeof(nameBuf), "chunk_%d_%d.json", popCx, popCy);
      shared::WorldMapFile blank;
      blank.width = S2; blank.height = S2;
      blank.spawnPoint = { S2 / 2, S2 / 2 };
      blank.tiles.assign(static_cast<std::size_t>(S2),
                         std::vector<shared::TileData>(static_cast<std::size_t>(S2)));
      for (int ty = 0; ty < S2; ++ty)
        for (int tx = 0; tx < S2; ++tx) {
          auto& t = blank.tiles[ty][tx];
          t.x = tx; t.y = ty;
        }
      blank.vertexHeights.assign(static_cast<std::size_t>((S2 + 1) * (S2 + 1)), 0.0f);
      const auto path = worldDir() / nameBuf;
      if (shared::saveWorldMap(path, blank)) {
        worldAssignCell(popCx, popCy, nameBuf);
        worldThumbs_.erase(nameBuf);   // (re)rasterise if a stale thumb existed
      }
    }

    ImGui::Separator();
    ImGui::TextDisabled("Existing maps:");

    // List .json maps in the world dir (excluding world.json itself).
    std::error_code ec;
    int shown = 0;
    for (const auto& e : std::filesystem::directory_iterator(worldDir(), ec)) {
      if (ec) break;
      if (!e.is_regular_file() || e.path().extension() != ".json") continue;
      const std::string fn = e.path().filename().string();
      if (fn == "world.json") continue;
      if (ImGui::MenuItem(fn.c_str()))
        worldAssignCell(popCx, popCy, fn);
      if (++shown >= 40) { ImGui::TextDisabled("..."); break; }
    }
    if (shown == 0) ImGui::TextDisabled("(none found)");
    ImGui::EndPopup();
  }

  ImGui::End();
}

}  // namespace editor
