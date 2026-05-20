#pragma once

// Lightweight synchronous HTTP client for the entity DB API.
// WinHTTP implementation lives in EntityClient.cpp to keep windows.h
// macros out of headers (they break glaze template instantiation).

#include "EntityDefs.hpp"
#include <glaze/glaze.hpp>

#include <string>
#include <vector>

// ---- Glaze JSON meta -------------------------------------------------------

template<> struct glz::meta<editor::ActionDef> {
  using T = editor::ActionDef;
  static constexpr auto value = glz::object(
    "id",           &T::id,
    "display_name", &T::displayName,
    "handler_type", &T::handlerType);
};

template<> struct glz::meta<editor::DropEntry> {
  using T = editor::DropEntry;
  static constexpr auto value = glz::object(
    "item_id",  &T::itemId,
    "quantity", &T::quantity,
    "rate",     &T::rate);
};

template<> struct glz::meta<editor::ObjectDef> {
  using T = editor::ObjectDef;
  static constexpr auto value = glz::object(
    "id",              &T::id,
    "name",            &T::name,
    "model_path",      &T::modelPath,
    "object_type",     &T::objectType,
    "collision",       &T::collision,
    "size_x",          &T::sizeX,
    "size_y",          &T::sizeY,
    "action_id",       &T::actionId,
    "required_skill",  &T::requiredSkill,
    "required_level",  &T::requiredLevel,
    "drop_item_id",    &T::dropItemId,
    "drop_quantity",   &T::dropQuantity,
    "respawn_ticks",   &T::respawnTicks,
    "craft_action_id", &T::craftActionId,
    "examine_text",    &T::examineText);
};

template<> struct glz::meta<editor::NpcDef> {
  using T = editor::NpcDef;
  static constexpr auto value = glz::object(
    "id",                 &T::id,
    "name",               &T::name,
    "model_path",         &T::modelPath,
    "size_x",             &T::sizeX,
    "size_y",             &T::sizeY,
    "is_attackable",      &T::isAttackable,
    "max_hp",             &T::maxHp,
    "attack",             &T::attack,
    "strength",           &T::strength,
    "melee_defense",      &T::meleeDefense,
    "ranged_defense",     &T::rangedDefense,
    "attack_speed_ticks", &T::attackSpeedTicks,
    "respawn_ticks",      &T::respawnTicks,
    "is_talkable",        &T::isTalkable,
    "dialogue",           &T::dialogue,
    "ai",                 &T::ai,
    "examine_text",       &T::examineText,
    "drops",              &T::drops);
};

template<> struct glz::meta<editor::ItemDef> {
  using T = editor::ItemDef;
  static constexpr auto value = glz::object(
    "id",              &T::id,
    "name",            &T::name,
    "stackable",       &T::stackable,
    "tradable",        &T::tradable,
    "value",           &T::value,
    "examine_text",    &T::examineText,
    "item_type",       &T::itemType,
    "equip_slot",      &T::equipSlot,
    "two_handed",      &T::twoHanded,
    "melee_attack",    &T::meleeAttack,
    "melee_strength",  &T::meleeStrength,
    "melee_defense",   &T::meleeDefense,
    "ranged_attack",   &T::rangedAttack,
    "ranged_strength", &T::rangedStrength,
    "ranged_defense",  &T::rangedDefense,
    "required_skill",  &T::requiredSkill,
    "required_level",  &T::requiredLevel,
    "tool_type",       &T::toolType,
    "combat_style",    &T::combatStyle,
    "heal_amount",     &T::healAmount,
    "sprite_path",     &T::spritePath,
    "model_dropped",   &T::modelDropped,
    "model_equipped",  &T::modelEquipped);
};

// ---- HTTP helper (implemented in EntityClient.cpp with WinHTTP) ------------

namespace editor {

// Synchronous HTTP request to localhost:8080. Throws std::runtime_error on failure.
std::string entityHttpRequest(const std::wstring& method,
                              const std::wstring& path,
                              const std::string&  body = {});

// Wide-string helper (also in EntityClient.cpp)
std::wstring entityToWide(const std::string& s);

// ---- EntityClient ----------------------------------------------------------

struct EntityClient {
  // ---- Items
  std::vector<ItemDef> getItems() {
    auto json = entityHttpRequest(L"GET", L"/api/db/items");
    std::vector<ItemDef> out;
    glz::read_json(out, json);
    return out;
  }
  bool saveItem(const ItemDef& d, bool isNew) {
    auto json = glz::write_json(d);
    if (!json) return false;
    try {
      if (isNew) entityHttpRequest(L"POST", L"/api/db/items", *json);
      else       entityHttpRequest(L"PUT",  entityToWide("/api/db/items/" + d.id), *json);
      return true;
    } catch (...) { return false; }
  }
  bool deleteItem(const std::string& id) {
    try { entityHttpRequest(L"DELETE", entityToWide("/api/db/items/" + id)); return true; }
    catch (...) { return false; }
  }

  // ---- NPCs
  std::vector<NpcDef> getNPCs() {
    auto json = entityHttpRequest(L"GET", L"/api/db/npcs");
    std::vector<NpcDef> out;
    glz::read_json(out, json);
    return out;
  }
  bool saveNPC(const NpcDef& d, bool isNew) {
    auto json = glz::write_json(d);
    if (!json) return false;
    try {
      if (isNew) entityHttpRequest(L"POST", L"/api/db/npcs", *json);
      else       entityHttpRequest(L"PUT",  entityToWide("/api/db/npcs/" + d.id), *json);
      return true;
    } catch (...) { return false; }
  }
  bool deleteNPC(const std::string& id) {
    try { entityHttpRequest(L"DELETE", entityToWide("/api/db/npcs/" + id)); return true; }
    catch (...) { return false; }
  }

  // ---- Objects
  std::vector<ObjectDef> getObjects() {
    auto json = entityHttpRequest(L"GET", L"/api/db/objects");
    std::vector<ObjectDef> out;
    glz::read_json(out, json);
    return out;
  }
  bool saveObject(const ObjectDef& d, bool isNew) {
    auto json = glz::write_json(d);
    if (!json) return false;
    try {
      if (isNew) entityHttpRequest(L"POST", L"/api/db/objects", *json);
      else       entityHttpRequest(L"PUT",  entityToWide("/api/db/objects/" + d.id), *json);
      return true;
    } catch (...) { return false; }
  }
  bool deleteObject(const std::string& id) {
    try { entityHttpRequest(L"DELETE", entityToWide("/api/db/objects/" + id)); return true; }
    catch (...) { return false; }
  }

  // ---- Actions
  std::vector<ActionDef> getActions() {
    auto json = entityHttpRequest(L"GET", L"/api/db/actions");
    std::vector<ActionDef> out;
    glz::read_json(out, json);
    return out;
  }
  bool saveAction(const ActionDef& d, bool isNew) {
    auto json = glz::write_json(d);
    if (!json) return false;
    try {
      if (isNew) entityHttpRequest(L"POST", L"/api/db/actions", *json);
      else       entityHttpRequest(L"PUT",  entityToWide("/api/db/actions/" + d.id), *json);
      return true;
    } catch (...) { return false; }
  }
  bool deleteAction(const std::string& id) {
    try { entityHttpRequest(L"DELETE", entityToWide("/api/db/actions/" + id)); return true; }
    catch (...) { return false; }
  }
};

}  // namespace editor
