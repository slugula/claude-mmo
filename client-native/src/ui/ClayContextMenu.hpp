#pragma once
// ClayContextMenu — OSRS-style right-click context menu rendered as a
// Clay floating panel.  App.cpp populates the entries; clayFrame builds the
// layout; handleContextMenuInput() detects which entry (if any) was clicked.

#include <string>
#include <vector>

namespace ui {

// Single menu entry.  App.cpp fills verb/subject; the entry index is used
// by the caller to dispatch the corresponding action after Clay input.
struct CtxMenuEntry {
    std::string verb;     // "Chop down", "Attack", "Take", …
    std::string subject;  // "Tree", "Chicken", item name, …
    // Optional extra label appended in grey (e.g. quantity hints) — unused for now.
};

// Singleton state shared between App.cpp (writer) and ClayContextMenu.cpp (reader).
struct CtxMenuState {
    bool                       open   = false;
    float                      x      = 0.f;   // click screen position
    float                      y      = 0.f;
    float                      screenW = 1920.f;
    float                      screenH = 1080.f;
    std::vector<CtxMenuEntry>  entries;
    int                        clickedIndex = -1;  // set by handleContextMenuInput
};

// Global accessor — zero-initialised on first use.
CtxMenuState& ctxMenu();

// Called between Clay_BeginLayout / Clay_EndLayout to emit the floating panel.
void buildContextMenu();

// Called after Clay_EndLayout.  Detects clicks on entries and sets clickedIndex.
// Returns true if the menu should close (any entry clicked, or click outside).
bool handleContextMenuInput(bool leftClicked, float mx, float my);

} // namespace ui
