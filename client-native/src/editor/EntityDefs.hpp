#pragma once
#include <string>
#include <vector>

namespace editor {

struct ActionDef {
  std::string id;
  std::string displayName;
  std::string handlerType;  // gather_resource | production_facility | equip | eat | talk | bank | examine
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
  std::string depletedModel;          // mesh shown while the node is depleted (empty = render nothing)
};

struct DropEntry {
  std::string itemId;
  int         quantity = 1;
  float       rate     = 1.0f;
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
};

}  // namespace editor
