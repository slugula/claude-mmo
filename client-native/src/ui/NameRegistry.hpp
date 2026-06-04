#pragma once

// Populated once at App startup from the DB API; read-only after that.
// Provides player-facing display names for NPC kinds and item IDs.
// Both App.cpp and Panels.cpp include this header.

#include <cctype>
#include <string>
#include <unordered_map>

namespace ui {

// ---- Storage (inline so the single definition lives in the header) ----------

inline std::unordered_map<std::string, std::string> g_npcNames;   // kind  → "Chicken"
inline std::unordered_map<std::string, std::string> g_itemNames;  // id    → "Bronze sword"
inline std::unordered_map<std::string, std::string> g_skillNames; // id    → "Cowboy"
inline std::unordered_map<std::string, bool>        g_npcAttackable; // kind → isAttackable

// ---- Fallback prettification (id/kind → human-readable) --------------------
// e.g. "bronze_sword" → "Bronze sword",  "shopkeeper" → "Shopkeeper"

inline std::string prettifyId(const std::string& id) {
    if (id.empty()) return {};
    std::string out;
    out.reserve(id.size());
    bool first = true;
    for (char ch : id) {
        if (ch == '_' || ch == '-') { out.push_back(' '); }
        else if (first) { out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch)))); first = false; }
        else { out.push_back(ch); }
    }
    return out;
}

// ---- Lookup helpers ---------------------------------------------------------

// Returns the DB display name for an NPC kind, or a prettified fallback.
inline std::string npcName(const std::string& kind) {
    auto it = g_npcNames.find(kind);
    return (it != g_npcNames.end() && !it->second.empty()) ? it->second : prettifyId(kind);
}

// Returns the DB display name for an item id, or a prettified fallback.
inline std::string itemName(const std::string& id) {
    auto it = g_itemNames.find(id);
    return (it != g_itemNames.end() && !it->second.empty()) ? it->second : prettifyId(id);
}

// Returns the DB display name for a skill id (editor-authored), or a
// prettified fallback. Lets renames like gunner→"Cowboy" propagate everywhere.
inline std::string skillName(const std::string& id) {
    auto it = g_skillNames.find(id);
    return (it != g_skillNames.end() && !it->second.empty()) ? it->second : prettifyId(id);
}

// Returns true when the NPC kind is flagged as attackable in the DB.
// Falls back to false (safe default) when offline.
inline bool npcIsAttackable(const std::string& kind) {
    auto it = g_npcAttackable.find(kind);
    return (it != g_npcAttackable.end()) ? it->second : false;
}

}  // namespace ui
