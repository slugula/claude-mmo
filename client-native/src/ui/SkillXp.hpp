#pragma once
// Shared skill XP helpers: the OSRS XP table + per-level math + the canonical
// skill accent colors. Header-only so the HUD skills panel and the XP tracker
// agree on level boundaries, progress, and color.

#include <array>
#include <cstdint>
#include <string>

namespace ui {

// xp required to REACH level (index n => level n+2). Mirrors the server table.
inline constexpr std::array<int, 98> kXpTable = {
    83,174,276,388,512,650,801,969,1154,1358,1584,1833,2107,2411,2746,3115,3523,
    3973,4470,5018,5624,6291,7028,7842,8740,9730,10824,12031,13363,14833,16456,
    18247,20224,22406,24815,27473,30408,33648,37224,41171,45529,50339,55649,
    61512,67983,75127,83014,91721,101333,111945,123660,136594,150872,166636,
    184040,203254,224466,247886,273742,302288,333804,368599,407015,449428,
    496254,547953,605032,668051,737627,814445,899257,992895,1096278,1210421,
    1336443,1475581,1629200,1798808,1986068,2192818,2421087,2673114,2951373,
    3258594,3597792,3972294,4385776,4842295,5346332,5902831,6517253,7195629,
    7944614,8771558,9684577,10692629,11805606,13034431,
};

inline int xpForLevel(int lvl) {
    if (lvl <= 1)  return 0;
    if (lvl >= 99) return kXpTable[97];
    return kXpTable[lvl - 2];
}

// Fraction [0,1] through the current level given total xp + current level.
inline float levelProgress(double totalXp, int level) {
    if (level >= 99) return 1.0f;
    const double a = xpForLevel(level);
    const double b = xpForLevel(level + 1);
    if (b <= a) return 0.0f;
    double t = (totalXp - a) / (b - a);
    if (t < 0.0) t = 0.0; if (t > 1.0) t = 1.0;
    return static_cast<float>(t);
}

// Canonical accent color for a skill id (RGBA 0..255). Matches the HUD palette.
struct SkillColor { uint8_t r, g, b; };
inline SkillColor skillColor(const std::string& id) {
    if (id == "hitpoints")   return { 220,  40,  40 };
    if (id == "defence")     return {  60, 120, 220 };
    if (id == "warrior")     return { 200, 136,  44 };
    if (id == "gunner")      return {   0, 207, 255 };
    if (id == "woodcutting") return {  80, 144,  64 };
    if (id == "mining")      return { 150, 150, 160 };
    if (id == "fishing")     return {  90, 160, 220 };
    if (id == "cooking")     return { 200,  90,  60 };
    return { 235, 215, 120 };  // default gold
}

} // namespace ui
