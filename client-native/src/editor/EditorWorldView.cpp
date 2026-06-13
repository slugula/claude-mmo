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
  // Assemble + enter world mode immediately so the whole world is editable.
  enterWorldMode(worldManifestPath_);
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
}

void EditorApp::worldEraseCell(int cx, int cy) {
  auto& cs = worldManifest_.chunks;
  cs.erase(std::remove_if(cs.begin(), cs.end(),
           [&](const shared::WorldChunkRef& c) { return c.cx == cx && c.cy == cy; }),
           cs.end());
  worldDirty_ = true;
}

void EditorApp::worldOpenChunk(int cx, int cy) {
  if (!worldCellAt(cx, cy)) return;
  // (Re)assemble when not yet in world mode, or when this cell isn't in the
  // assembled world yet (e.g. just created). Otherwise just recenter — every
  // chunk is already loaded and editable.
  const bool needAssemble = !worldMode_ || !assignedCells_.count({ cx, cy });
  if (needAssemble && !worldManifestPath_.empty()) enterWorldMode(worldManifestPath_);
  worldFocusCell(cx, cy);
  setMode(EditorMode::Map);   // jump to the map workspace to edit it
}

void EditorApp::worldDestroyThumbs() {
  for (auto& [file, tex] : worldThumbs_)
    if (tex != 0) glDeleteTextures(1, &tex);
  worldThumbs_.clear();
}

// Full-detail thumbnail (4px/tile: terrain + water/overlays + wall lines),
// rasterised once per mapFile via the shared minimap raster so it matches the
// in-game minimap 1:1. Texture is native orientation (+tileX → +U); the World
// view flips U on display so east reads left, like the map view. Failure caches
// 0 so we don't retry every frame.
GLuint EditorApp::worldThumbnail(const std::string& mapFile) {
  if (auto it = worldThumbs_.find(mapFile); it != worldThumbs_.end())
    return it->second;

  GLuint tex = 0;
  shared::WorldMapFile m;
  if (shared::loadWorldMap(worldDir() / mapFile, m) && m.width > 0 && m.height > 0) {
    std::vector<uint8_t> buf; int w = 0, h = 0;
    editor::rasterMapBase(m, 4, buf, w, h);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
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

// One-time autoload of the canonical public/maps/world.json so neighbor ghosts
// resolve even before the World View window has been opened (e.g. opening an
// assigned map straight from File ▸ Open on a fresh launch).
void EditorApp::worldEnsureManifestLoaded() {
  if (worldAutoloaded_) return;
  worldAutoloaded_ = true;
  const auto p = canonicalMapsDir() / "world.json";
  std::error_code ec;
  if (!std::filesystem::exists(p, ec)) return;
  shared::WorldManifest loaded;
  if (shared::loadWorldManifest(p, loaded)) {
    worldManifest_     = std::move(loaded);
    worldManifestPath_ = p.string();
    worldDirty_ = false;
  }
}

// ---- World editing mode (assemble whole world, edit, slice-save) ------------

void EditorApp::enterWorldMode(const std::string& manifestPath) {
  shared::WorldManifest m;
  if (!shared::loadWorldManifest(manifestPath, m)) return;
  worldManifest_     = m;
  worldManifestPath_ = manifestPath;
  chunkSize_         = m.chunkSize > 0 ? m.chunkSize : 64;
  const auto baseDir = std::filesystem::path(manifestPath).parent_path();

  // One-time self-check of the assemble/slice flip math during bring-up.
  static bool verified = false;
  if (!verified) { verified = true; editor::verifyAssembleRoundTrip(m, baseDir); }

  shared::WorldMapFile assembled;
  if (!editor::assembleWorld(m, baseDir, assembled, assignedCells_)) return;

  map_       = std::move(assembled);
  npcSpawns_ = map_.npcSpawns;
  worldMode_ = true;
  worldDirty_ = false;
  dirtyCells_.clear();
  currentFilePath_.clear();
  activeCell_ = { chunkSize_ > 0 ? m.spawn.x / chunkSize_ : 0,
                  chunkSize_ > 0 ? m.spawn.y / chunkSize_ : 0 };

  rebuildObstacles();
  waterRenderer_.rebuild(map_, waterUniforms_.waterOffset);
  minimap_.init(map_.width, map_.height); minimap_.rebuild(map_, npcSpawns_);
  undo_.clear(); pushUndo();
  worldFocusCell(activeCell_.first, activeCell_.second);
  updateWindowTitle();
}

void EditorApp::worldFocusCell(int cx, int cy) {
  activeCell_ = { cx, cy };
  rebuildTerrainGL();   // rebuild the draw ring centred on the new active cell
  const float wx = cx * chunkSize_ + chunkSize_ * 0.5f;
  const float wz = cy * chunkSize_ + chunkSize_ * 0.5f;
  camera_.snapTo({ wx, 0.0f, wz });
}

void EditorApp::worldSaveDirtyChunks() {
  if (!worldMode_) return;
  const auto dir = worldDir();
  int saved = 0;
  for (const auto& cell : dirtyCells_) {
    const auto* ref = worldCellAt(cell.first, cell.second);
    if (!ref) continue;
    shared::WorldMapFile sl =
        editor::sliceChunk(map_, cell.first, cell.second, chunkSize_, worldManifest_.spawn);
    if (shared::saveWorldMap(dir / ref->mapFile, sl)) {
      ++saved;
      worldThumbs_.erase(ref->mapFile);   // world-view thumbnail re-rasters
    }
  }
  dirtyCells_.clear();
  dirty_ = false;
  if (worldDirty_ && !worldManifestPath_.empty() &&
      shared::saveWorldManifest(worldManifestPath_, worldManifest_))
    worldDirty_ = false;
  updateWindowTitle();
  std::fprintf(stdout, "[worldSave] saved %d dirty chunk(s)\n", saved);
}

void EditorApp::markCellDirtyAtTile(int gx, int gy) {
  if (!worldMode_ || chunkSize_ <= 0) return;
  const CellKey c{ gx / chunkSize_, gy / chunkSize_ };
  if (assignedCells_.count(c)) dirtyCells_.insert(c);
}

// ---- World View window ------------------------------------------------------

void EditorApp::drawWorldView() {
  // Populate the grid from world.json the first time the window is shown.
  if (!worldAutoloaded_) worldEnsureManifestLoaded();

  // Workspace pane: renderFrame pins position/size to the content area right
  // of the mode rail, so this window is fixed (no title bar, close via rail).
  const ImGuiWindowFlags paneFlags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoDocking  | ImGuiWindowFlags_NoBringToFrontOnFocus;
  if (!ImGui::Begin("##worldview", nullptr, paneFlags)) { ImGui::End(); return; }

  ImGui::TextUnformatted(worldManifestPath_.empty()
      ? "World Editor — unsaved world"
      : ("World Editor — " + std::filesystem::path(worldManifestPath_).filename().string() +
         (worldDirty_ ? " *" : "")).c_str());
  ImGui::SameLine(0.0f, 24.0f);

  // ---- Toolbar row ----
  if (ImGui::Button("New World"))  worldNewManifest();
  ImGui::SameLine();
  if (ImGui::Button("Open World...")) worldOpenManifest();
  ImGui::SameLine();
  if (ImGui::Button("Save World")) worldSaveManifest();
  ImGui::SameLine();
  if (ImGui::Button("Edit World")) {   // assemble + enter full-detail world editing
    if (!worldManifestPath_.empty()) { enterWorldMode(worldManifestPath_); setMode(EditorMode::Map); }
  }
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

  // Visible grid extent: bounding box of assigned chunks plus margin, at least 4×4.
  int maxCx = 3, maxCy = 3;
  for (const auto& c : worldManifest_.chunks) {
    maxCx = std::max(maxCx, c.cx + 2);
    maxCy = std::max(maxCy, c.cy + 2);
  }

  const float z  = worldZoom_;
  const float ox = canvasPos.x + worldOffX_ + 16.0f;
  const float oy = canvasPos.y + worldOffY_ + 16.0f;
  // Horizontal flip: higher cx (further east) draws further LEFT, matching the
  // map view and 3D (lookAtLH puts +tileX/east on the screen-left). Without this
  // the World grid disagreed with where chunks actually sit in-game.
  auto cellMin = [&](int cx, int cy) { return ImVec2(ox + (maxCx - cx) * z, oy + cy * z); };

  // Hovered cell under the mouse (non-negative cells only — v1 world space).
  int hovCx = -1, hovCy = -1;
  if (hovered) {
    const float fy = (io.MousePos.y - oy) / z;
    const int   col = static_cast<int>(std::floor((io.MousePos.x - ox) / z));  // screen column from left
    const int   cx  = maxCx - col;                                            // inverse of the flip
    if (cx >= 0 && fy >= 0.0f) { hovCx = cx; hovCy = static_cast<int>(fy); }
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
          // Flip U so the thumbnail's +tileX (east) reads on the left, matching
          // the flipped grid and the map view.
          dl->AddImage((ImTextureID)(uintptr_t)tex, a, b, ImVec2(1, 0), ImVec2(0, 1));
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
        // Open it for editing right away: since it's now assigned to this cell,
        // worldOpenChunk loads the correct neighbor ghosts around it.
        worldOpenChunk(popCx, popCy);
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
