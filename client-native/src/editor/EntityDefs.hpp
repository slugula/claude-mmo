#pragma once
#include <string>
#include <vector>

namespace editor {

struct ActionDef {
  std::string id;
  std::string displayName;
  std::string handlerType;  // gather_resource | production_facility | equip | eat | talk | bank | examine
};

// A single global tunable (game_config row): an integer knob with a friendly
// label + category for grouping in the editor.
struct ConfigDef {
  std::string key;
  int         value = 0;
  std::string label;
  std::string category;
};

struct SkillDef {
  std::string id;            // mirrors SkillId (warrior, defence, …)
  std::string name;
  std::string iconPath;      // assets/sprites/skills/<id>.png ("" = none)
  int         sortOrder = 0;
};

struct ObjectDef {
  std::string id;
  std::string name;
  std::string modelPath;
  std::string objectType   = "Decoration";      // Decoration | ResourceNode | ProductionFacility
  std::string collision    = "full_blocking";   // none | full_blocking | half_blocking
  int         sizeX        = 1;
  int         sizeY        = 1;
  // ResourceNode
  std::string actionId;
  std::string requiredSkill;
  int         requiredLevel = 0;
  std::string dropItemId;
  int         dropQuantity  = 1;
  int         respawnTicks  = 25;
  // ProductionFacility
  std::string craftActionId;
  std::string examineText;
  // Animation & orientation
  std::string defaultClip;            // glTF clip name to auto-play (empty = first clip)
  bool        looping    = true;
  float       rotationX  = 0.f;       // degrees, snapped to 90° increments in the editor
  float       rotationY  = 0.f;
  float       rotationZ  = 0.f;
  std::string depletedObjectId;       // another object id shown while depleted (empty = render nothing)
  bool        pickable   = true;      // hover outline + left-click pick
};

struct DropEntry {
  std::string itemId;
  int         quantity = 1;
  float       rate     = 1.0f;
};

// A production recipe (recipe_definitions): input item -> output at a facility
// object. failItemId empty = never fails; otherwise success scales with level
// from requiredLevel up to noFailLevel.
struct RecipeDef {
  std::string id;
  std::string facilityId;       // object id of the station
  std::string skill;            // SkillId trained
  int         requiredLevel = 1;
  float       xp            = 0.f;
  std::string inputItemId;
  int         inputQty      = 1;
  std::string outputItemId;
  int         outputQty     = 1;
  std::string failItemId;       // "" = never fails
  int         noFailLevel   = 99;
};

struct NpcDef {
  std::string id;
  std::string name;
  std::string modelPath;
  int         sizeX            = 1;
  int         sizeY            = 1;
  bool        isAttackable     = false;
  int         maxHp            = 1;
  int         attack           = 0;
  int         strength         = 0;
  int         meleeDefense     = 0;
  int         rangedDefense    = 0;
  int         attackSpeedTicks = 16;
  int         respawnTicks     = 150;
  bool        isTalkable       = false;
  std::string dialogue;
  std::string ai               = "static";      // static | wander
  std::string examineText;
  std::vector<DropEntry> drops;
};

struct ItemDef {
  std::string id;
  std::string name;
  bool        stackable    = false;
  bool        tradable     = true;
  int         value        = 0;
  std::string examineText;
  std::string itemType     = "resource";        // resource | equipment | food
  // Equipment
  std::string equipSlot;                         // head|body|legs|feet|hands|neck|ring|leftHand|rightHand|ammo
  bool        twoHanded    = false;
  int         meleeAttack  = 0;
  int         meleeStrength= 0;
  int         meleeDefense = 0;
  int         rangedAttack = 0;
  int         rangedStrength=0;
  int         rangedDefense= 0;
  std::string requiredSkill;
  int         requiredLevel= 0;
  std::string toolType;                          // axe | pickaxe
  std::string combatStyle;                       // melee | gunner
  // Food
  int         healAmount   = 0;
  // Assets
  std::string spritePath;
  std::string modelDropped;
  std::string modelEquipped;
  // Held-weapon grip: how modelEquipped sits in the hand socket. These are
  // RELATIVE to the current player model's hand bone — a new model needs them
  // re-tuned (pure data, no code change). gripJoint defaults via SkeletonConfig.
  std::string gripJoint;                         // "" = default (weapon_main socket)
  float       gripPosX = 0.0f, gripPosY = 0.0f, gripPosZ = 0.0f;
  float       gripRotX = 0.0f, gripRotY = 0.0f, gripRotZ = 0.0f;
  float       gripScale = 1.0f;
};

}  // namespace editor
