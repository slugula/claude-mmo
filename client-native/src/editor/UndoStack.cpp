#include "editor/UndoStack.hpp"

namespace editor {

void UndoStack::push(const shared::WorldMapFile& map,
                     const std::vector<shared::NpcSpawn>& npcs) {
  // Truncate any redo history above the current position.
  if (idx_ + 1 < static_cast<int>(entries_.size()))
    entries_.erase(entries_.begin() + idx_ + 1, entries_.end());

  // Evict oldest entry when full.
  if (static_cast<int>(entries_.size()) >= kMaxEntries) {
    entries_.erase(entries_.begin());
    if (idx_ > 0) --idx_;
  }

  entries_.push_back({ map, npcs });
  idx_ = static_cast<int>(entries_.size()) - 1;
}

const UndoStack::Snapshot& UndoStack::undo() {
  if (idx_ > 0) --idx_;
  return entries_[static_cast<std::size_t>(idx_)];
}

const UndoStack::Snapshot& UndoStack::redo() {
  if (idx_ < static_cast<int>(entries_.size()) - 1) ++idx_;
  return entries_[static_cast<std::size_t>(idx_)];
}

}  // namespace editor
