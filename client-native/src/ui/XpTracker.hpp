#pragma once
// XpTracker — animated XP drops + an XP tracker box, drawn on the ImGui
// foreground draw list (screen-space HUD, like WorldOverlays).
//
//  - On an XP gain, a "+N" drop with the skill icon appears just above screen
//    center and rises up into the tracker, fading.
//  - The tracker (top-center) is a bordered box: skill icon | total XP, with a
//    progress-to-next-level bar underneath. The total counts up and the bar
//    eases toward the new value; the box pulses on each gain. It fades out
//    after a few idle seconds.

#include "world/SpriteCache.hpp"

#include <string>
#include <vector>

namespace ui {

class XpTracker {
public:
    // Called when the local player gains XP in a skill (delta > 0).
    void pushGain(const std::string& skillId, int amount, double totalXp, int level);

    // Draw + advance animations. screenW/H and mouse are physical pixels;
    // uiScale matches the rest of the HUD so the box scales on HiDPI.
    void render(const SpriteCache* sprites, float screenW, float screenH,
                float dt, float uiScale);

private:
    struct Drop {
        std::string skillId;
        int         amount = 0;
        float       age    = 0.0f;   // seconds since spawn
        float       life   = 1.5f;   // total lifetime
        float       slot   = 0.0f;   // vertical stagger for overlapping drops
    };
    std::vector<Drop> drops_;

    // Tracker display state.
    bool        active_      = false;  // has the player gained XP this session?
    std::string skillId_;
    double      targetXp_    = 0.0;    // real total (snaps on gain)
    double      shownXp_     = 0.0;    // animated counter toward targetXp_
    int         level_       = 1;
    float       targetProg_  = 0.0f;   // real fraction through current level
    float       shownProg_   = 0.0f;   // animated bar fill
    float       sinceGain_   = 999.0f; // seconds since last gain (drives fade)
    float       pulse_       = 0.0f;   // 0..1 bump on each gain, decays
};

} // namespace ui
