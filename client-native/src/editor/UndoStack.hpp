#pragma once

#include "shared/SharedTypes.hpp"

#include <array>
#include <vector>

namespace editor {

// Snapshot-based undo/redo stack. Each entry stores a full copy of the
// WorldMapFile (tiles + heights) plus the NPC spawns and spawn point.
// Snapshot size for a 64×64 map ≈ 220 KB; 50 entries ≈ 11 MB — acceptable.
class UndoStack {
public:
  static constexpr int kMaxEntries = 50;

  struct Snapshot {
    shared::WorldMapFile          map;
    std::vector<shared::NpcSpawn> npcs;
  };

  // Push the current state onto the stack (truncates redo history).
  void push(const shared::WorldMapFile& map,
            const std::vector<shared::NpcSpawn>& npcs);

  // Returns true if there is a state to undo.
  bool canUndo() const { return idx_ > 0; }

  // Returns true if there is a state to redo.
  bool canRedo() const { return idx_ < static_cast<int>(entries_.size()) - 1; }

  // Move back one step and return the snapshot. Call canUndo() first.
  const Snapshot& undo();

  // Move forward one step and return the snapshot. Call canRedo() first.
  const Snapshot& redo();

  // Erase all history.
  void clear() { entries_.clear(); idx_ = -1; }

private:
  std::vector<Snapshot> entries_;
  int                   idx_ = -1;
};

}  // namespace editor
