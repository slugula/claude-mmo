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

template<> struct glz::meta<editor::ConfigDef> {
  using T = editor::ConfigDef;
  static constexpr auto value = glz::object(
    "key",      &T::key,
    "value",    &T::value,
    "label",    &T::label,
    "category", &T::category);
};

template<> struct glz::meta<editor::SkillDef> {
  using T = editor::SkillDef;
  static constexpr auto value = glz::object(
    "id",         &T::id,
    "name",       &T::name,
    "icon_path",  &T::iconPath,
    "sort_order", &T::sortOrder);
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
    "examine_text",    &T::examineText,
    "default_clip",    &T::defaultClip,
    "looping",         &T::looping,
    "rotation_x",        &T::rotationX,
    "rotation_y",        &T::rotationY,
    "rotation_z",        &T::rotationZ,
    "depleted_object_id",&T::depletedObjectId,
    "pickable",          &T::pickable);
};

template<> struct glz::meta<editor::RecipeDef> {
  using T = editor::RecipeDef;
  static constexpr auto value = glz::object(
    "id",             &T::id,
    "facility_id",    &T::facilityId,
    "skill",          &T::skill,
    "required_level", &T::requiredLevel,
    "xp",             &T::xp,
    "input_item_id",  &T::inputItemId,
    "input_qty",      &T::inputQty,
    "output_item_id", &T::outputItemId,
    "output_qty",     &T::outputQty,
    "fail_item_id",   &T::failItemId,
    "no_fail_level",  &T::noFailLevel);
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
    "model_equipped",  &T::modelEquipped,
    "grip_joint",      &T::gripJoint,
    "grip_pos_x",      &T::gripPosX,
    "grip_pos_y",      &T::gripPosY,
    "grip_pos_z",      &T::gripPosZ,
    "grip_rot_x",      &T::gripRotX,
    "grip_rot_y",      &T::gripRotY,
    "grip_rot_z",      &T::gripRotZ,
    "grip_scale",      &T::gripScale);
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
  std::string lastError;   // populated when a save/delete returns false

  // ---- Items
  std::vector<ItemDef> getItems() {
    auto json = entityHttpRequest(L"GET", L"/api/db/items");
    std::vector<ItemDef> out;
    (void)glz::read_json(out, json);
    return out;
  }
  bool saveItem(const ItemDef& d, bool isNew) {
    if (d.id.empty()) { lastError = "ID is required."; return false; }
    auto json = glz::write_json(d);
    if (!json) { lastError = "JSON serialization failed."; return false; }
    try {
      if (isNew) entityHttpRequest(L"POST", L"/api/db/items", *json);
      else       entityHttpRequest(L"PUT",  entityToWide("/api/db/items/" + d.id), *json);
      lastError.clear(); return true;
    } catch (const std::exception& e) { lastError = e.what(); return false; }
  }
  bool deleteItem(const std::string& id) {
    try { entityHttpRequest(L"DELETE", entityToWide("/api/db/items/" + id)); lastError.clear(); return true; }
    catch (const std::exception& e) { lastError = e.what(); return false; }
  }

  // ---- NPCs
  std::vector<NpcDef> getNPCs() {
    auto json = entityHttpRequest(L"GET", L"/api/db/npcs");
    std::vector<NpcDef> out;
    (void)glz::read_json(out, json);
    return out;
  }
  bool saveNPC(const NpcDef& d, bool isNew) {
    if (d.id.empty()) { lastError = "ID is required."; return false; }
    auto json = glz::write_json(d);
    if (!json) { lastError = "JSON serialization failed."; return false; }
    try {
      if (isNew) entityHttpRequest(L"POST", L"/api/db/npcs", *json);
      else       entityHttpRequest(L"PUT",  entityToWide("/api/db/npcs/" + d.id), *json);
      lastError.clear(); return true;
    } catch (const std::exception& e) { lastError = e.what(); return false; }
  }
  bool deleteNPC(const std::string& id) {
    try { entityHttpRequest(L"DELETE", entityToWide("/api/db/npcs/" + id)); lastError.clear(); return true; }
    catch (const std::exception& e) { lastError = e.what(); return false; }
  }

  // ---- Objects
  std::vector<ObjectDef> getObjects() {
    auto json = entityHttpRequest(L"GET", L"/api/db/objects");
    std::vector<ObjectDef> out;
    (void)glz::read_json(out, json);
    return out;
  }
  bool saveObject(const ObjectDef& d, bool isNew) {
    if (d.id.empty()) { lastError = "ID is required."; return false; }
    auto json = glz::write_json(d);
    if (!json) { lastError = "JSON serialization failed."; return false; }
    try {
      if (isNew) entityHttpRequest(L"POST", L"/api/db/objects", *json);
      else       entityHttpRequest(L"PUT",  entityToWide("/api/db/objects/" + d.id), *json);
      lastError.clear(); return true;
    } catch (const std::exception& e) { lastError = e.what(); return false; }
  }
  bool deleteObject(const std::string& id) {
    try { entityHttpRequest(L"DELETE", entityToWide("/api/db/objects/" + id)); lastError.clear(); return true; }
    catch (const std::exception& e) { lastError = e.what(); return false; }
  }

  // ---- Skills (fixed set; only the icon/name are editable, so update via PUT)
  std::vector<SkillDef> getSkills() {
    auto json = entityHttpRequest(L"GET", L"/api/db/skills");
    std::vector<SkillDef> out;
    (void)glz::read_json(out, json);
    return out;
  }
  bool saveSkill(const SkillDef& d) {
    if (d.id.empty()) { lastError = "ID is required."; return false; }
    auto json = glz::write_json(d);
    if (!json) { lastError = "JSON serialization failed."; return false; }
    try {
      entityHttpRequest(L"PUT", entityToWide("/api/db/skills/" + d.id), *json);
      lastError.clear(); return true;
    } catch (const std::exception& e) { lastError = e.what(); return false; }
  }

  // ---- Recipes (production: input item -> output at a facility object)
  std::vector<RecipeDef> getRecipes() {
    auto json = entityHttpRequest(L"GET", L"/api/db/recipes");
    std::vector<RecipeDef> out;
    (void)glz::read_json(out, json);
    return out;
  }
  bool saveRecipe(const RecipeDef& d, bool isNew) {
    if (d.id.empty()) { lastError = "ID is required."; return false; }
    auto json = glz::write_json(d);
    if (!json) { lastError = "JSON serialization failed."; return false; }
    try {
      if (isNew) entityHttpRequest(L"POST", L"/api/db/recipes", *json);
      else       entityHttpRequest(L"PUT",  entityToWide("/api/db/recipes/" + d.id), *json);
      lastError.clear(); return true;
    } catch (const std::exception& e) { lastError = e.what(); return false; }
  }
  bool deleteRecipe(const std::string& id) {
    try { entityHttpRequest(L"DELETE", entityToWide("/api/db/recipes/" + id)); lastError.clear(); return true; }
    catch (const std::exception& e) { lastError = e.what(); return false; }
  }

  // ---- Tunables (game_config). Saved in bulk; the server applies them live.
  std::vector<ConfigDef> getConfig() {
    auto json = entityHttpRequest(L"GET", L"/api/db/config");
    std::vector<ConfigDef> out;
    (void)glz::read_json(out, json);
    return out;
  }
  bool saveConfig(const std::vector<ConfigDef>& defs) {
    auto json = glz::write_json(defs);
    if (!json) { lastError = "JSON serialization failed."; return false; }
    try { entityHttpRequest(L"PUT", L"/api/db/config", *json); lastError.clear(); return true; }
    catch (const std::exception& e) { lastError = e.what(); return false; }
  }

  // ---- Actions
  std::vector<ActionDef> getActions() {
    auto json = entityHttpRequest(L"GET", L"/api/db/actions");
    std::vector<ActionDef> out;
    (void)glz::read_json(out, json);
    return out;
  }
  bool saveAction(const ActionDef& d, bool isNew) {
    if (d.id.empty()) { lastError = "ID is required."; return false; }
    auto json = glz::write_json(d);
    if (!json) { lastError = "JSON serialization failed."; return false; }
    try {
      if (isNew) entityHttpRequest(L"POST", L"/api/db/actions", *json);
      else       entityHttpRequest(L"PUT",  entityToWide("/api/db/actions/" + d.id), *json);
      lastError.clear(); return true;
    } catch (const std::exception& e) { lastError = e.what(); return false; }
  }
  bool deleteAction(const std::string& id) {
    try { entityHttpRequest(L"DELETE", entityToWide("/api/db/actions/" + id)); lastError.clear(); return true; }
    catch (const std::exception& e) { lastError = e.what(); return false; }
  }
};

}  // namespace editor
