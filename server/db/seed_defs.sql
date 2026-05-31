-- Auto-generated from the dev DB: authoritative entity definitions for production.
-- Apply on the server:  psql "$DATABASE_URL" -f server/db/seed_defs.sql

INSERT INTO item_definitions (id,name,stackable,tradable,value,examine_text,item_type,equip_slot,two_handed,melee_attack,melee_strength,melee_defense,ranged_attack,ranged_strength,ranged_defense,required_skill,required_level,tool_type,combat_style,heal_amount,sprite_path,model_dropped,model_equipped) VALUES
  ('amulet','Amulet of str',FALSE,TRUE,200,NULL,'equipment','neck',FALSE,0,4,0,0,0,0,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL),
  ('arrow','Bronze arrow',TRUE,TRUE,1,NULL,'equipment','ammo',FALSE,0,0,0,0,0,0,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL),
  ('axe','Bronze axe',FALSE,TRUE,16,NULL,'equipment','rightHand',FALSE,4,0,0,0,0,0,'woodcutting',1,'axe',NULL,NULL,NULL,NULL,NULL),
  ('basic_chaingun','Basic Chaingun',FALSE,TRUE,200,'A heavy two-handed energy weapon.','equipment','rightHand',TRUE,0,0,0,8,4,0,NULL,NULL,NULL,'gunner',NULL,NULL,NULL,NULL),
  ('bronze_bar','Bronze bar',FALSE,TRUE,20,NULL,'resource',NULL,FALSE,0,0,0,0,0,0,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL),
  ('bronze_helm','Bronze helm',FALSE,TRUE,80,NULL,'equipment','head',FALSE,0,0,3,0,0,0,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL),
  ('bronze_longsword','Bronze longsword',FALSE,TRUE,60,'Careful not to poke your eye out!','equipment','rightHand',FALSE,8,7,0,0,0,0,NULL,NULL,NULL,'melee',NULL,NULL,NULL,NULL),
  ('bronze_shield','Bronze shield',FALSE,TRUE,30,NULL,'equipment','leftHand',FALSE,0,0,5,0,0,0,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL),
  ('bronze_sword','Bronze sword',FALSE,TRUE,40,NULL,'equipment','rightHand',FALSE,6,3,0,0,0,0,NULL,NULL,NULL,'melee',NULL,NULL,NULL,NULL),
  ('coins','Coins',TRUE,TRUE,1,NULL,'resource',NULL,FALSE,0,0,0,0,0,0,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL),
  ('copper_ore','Copper ore',FALSE,TRUE,8,NULL,'resource',NULL,FALSE,0,0,0,0,0,0,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL),
  ('egg','Egg',FALSE,TRUE,2,'','resource','',FALSE,0,0,0,0,0,0,'',0,'','',0,'assets/sprites/items/Egg.png','',''),
  ('fishing_rod','Fishing rod',FALSE,TRUE,5,'Used to catch fish.','resource','',FALSE,0,0,0,0,0,0,'fishing',1,'fishing_rod','',0,'','',''),
  ('gold_ring','Gold ring',FALSE,TRUE,50,NULL,'equipment','ring',FALSE,0,0,0,0,0,0,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL),
  ('iron_axe','Iron axe',FALSE,TRUE,8,'','equipment','rightHand',FALSE,-1,0,2,0,0,0,'woodcutting',1,'axe','',0,'assets/sprites/items/woodcutting.png','',''),
  ('iron_bar','Iron bar',FALSE,TRUE,55,NULL,'resource',NULL,FALSE,0,0,0,0,0,0,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL),
  ('iron_ore','Iron ore',FALSE,TRUE,30,NULL,'resource',NULL,FALSE,0,0,0,0,0,0,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL),
  ('iron_sword','Iron sword',FALSE,TRUE,120,NULL,'equipment','rightHand',FALSE,10,5,0,0,0,0,NULL,NULL,NULL,'melee',NULL,NULL,NULL,NULL),
  ('kinetic_charges','Kinetic Charges',TRUE,TRUE,1,NULL,'equipment','ammo',FALSE,0,0,0,0,0,0,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL),
  ('leather_body','Leather body',FALSE,TRUE,30,NULL,'equipment','body',FALSE,0,0,2,0,0,0,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL),
  ('leather_boots','Leather boots',FALSE,TRUE,15,NULL,'equipment','feet',FALSE,0,0,1,0,0,0,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL),
  ('leather_gloves','Leather gloves',FALSE,TRUE,10,NULL,'equipment','hands',FALSE,0,0,1,0,0,0,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL),
  ('leather_helm','Leather helm',FALSE,TRUE,20,NULL,'equipment','head',FALSE,0,0,1,0,0,0,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL),
  ('leather_legs','Leather legs',FALSE,TRUE,25,NULL,'equipment','legs',FALSE,0,0,1,0,0,0,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL),
  ('logs','Logs',FALSE,TRUE,10,'Yup.','resource','',FALSE,0,0,0,0,0,0,'',0,'','',0,'assets/sprites/items/Log.png','',''),
  ('oak_logs','Oak logs',FALSE,TRUE,25,NULL,'resource',NULL,FALSE,0,0,0,0,0,0,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL),
  ('pickaxe','Iron Pickaxe',FALSE,TRUE,25,'A sturdy iron pickaxe, good for mining.','equipment','rightHand',FALSE,6,0,1,0,0,0,'mining',1,'pickaxe',NULL,NULL,NULL,NULL,NULL),
  ('raw_shrimp','Raw shrimp',FALSE,TRUE,5,'','resource','',FALSE,0,0,0,0,0,0,'',0,'','',0,'assets/sprites/items/Raw_Shrimp.png','',''),
  ('raw_trout','Raw trout',FALSE,TRUE,15,NULL,'resource',NULL,FALSE,0,0,0,0,0,0,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL),
  ('shrimp','Shrimp',FALSE,TRUE,10,NULL,'food',NULL,FALSE,0,0,0,0,0,0,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL),
  ('tin_ore','Tin ore',FALSE,TRUE,8,NULL,'resource',NULL,FALSE,0,0,0,0,0,0,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL),
  ('tinderbox','Tinderbox',FALSE,TRUE,1,NULL,'resource',NULL,FALSE,0,0,0,0,0,0,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL),
  ('trout','Trout',FALSE,TRUE,30,NULL,'food',NULL,FALSE,0,0,0,0,0,0,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL),
  ('willow_logs','Willow logs',FALSE,TRUE,40,NULL,'resource',NULL,FALSE,0,0,0,0,0,0,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL)
ON CONFLICT (id) DO UPDATE SET name=EXCLUDED.name, stackable=EXCLUDED.stackable, tradable=EXCLUDED.tradable, value=EXCLUDED.value, examine_text=EXCLUDED.examine_text, item_type=EXCLUDED.item_type, equip_slot=EXCLUDED.equip_slot, two_handed=EXCLUDED.two_handed, melee_attack=EXCLUDED.melee_attack, melee_strength=EXCLUDED.melee_strength, melee_defense=EXCLUDED.melee_defense, ranged_attack=EXCLUDED.ranged_attack, ranged_strength=EXCLUDED.ranged_strength, ranged_defense=EXCLUDED.ranged_defense, required_skill=EXCLUDED.required_skill, required_level=EXCLUDED.required_level, tool_type=EXCLUDED.tool_type, combat_style=EXCLUDED.combat_style, heal_amount=EXCLUDED.heal_amount, sprite_path=EXCLUDED.sprite_path, model_dropped=EXCLUDED.model_dropped, model_equipped=EXCLUDED.model_equipped;

INSERT INTO action_definitions (id,display_name,handler_type) VALUES
  ('bank','Bank','bank'),
  ('chop','Chop','gather_resource'),
  ('cook','Cook','production_facility'),
  ('eat','Eat','eat'),
  ('equip','Equip','equip'),
  ('examine','Examine','examine'),
  ('fish','Fish','gather_resource'),
  ('harvest','Harvest','gather_resource'),
  ('mine','Mine','gather_resource'),
  ('smith','Smith','production_facility'),
  ('talk','Talk-to','talk')
ON CONFLICT (id) DO UPDATE SET display_name=EXCLUDED.display_name, handler_type=EXCLUDED.handler_type;

INSERT INTO object_definitions (id,name,model_path,object_type,collision,size_x,size_y,action_id,required_skill,required_level,drop_item_id,drop_quantity,respawn_ticks,craft_action_id,examine_text,default_clip,looping,rotation_x,rotation_y,rotation_z,depleted_object_id,pickable) VALUES
  ('chest','Chest',NULL,'Decoration','full_blocking',1,1,'bank',NULL,NULL,NULL,1,0,NULL,'A secure bank chest.',NULL,TRUE,0,0,0,NULL,TRUE),
  ('fence','Fence',NULL,'Decoration','half_blocking',1,1,NULL,NULL,NULL,NULL,1,0,NULL,'A wooden fence.',NULL,TRUE,0,0,0,NULL,TRUE),
  ('fishing_spot','Fishing Spot','assets/models/fishing_spot.glb','ResourceNode','none',1,1,'fish',NULL,1,'raw_shrimp',1,10,NULL,'A calm fishing spot.','Swimming.001',TRUE,0,0,0,NULL,TRUE),
  ('rock','Rock','assets/models/Rock.glb','ResourceNode','full_blocking',1,1,'mine','mining',1,'copper_ore',1,50,NULL,'A rocky outcrop.',NULL,TRUE,0,0,0,NULL,TRUE),
  ('tree','Tree','assets/models/Tree.glb','ResourceNode','full_blocking',1,1,'chop','woodcutting',1,'logs',1,25,NULL,'A sturdy tree.',NULL,TRUE,0,0,0,'tree_stump',TRUE),
  ('tree_stump','Tree Stump','assets/models/Tree_Stump.glb','Decoration','full_blocking',1,1,NULL,NULL,0,NULL,1,25,NULL,'A simple tree stump.',NULL,TRUE,0,0,0,NULL,FALSE)
ON CONFLICT (id) DO UPDATE SET name=EXCLUDED.name, model_path=EXCLUDED.model_path, object_type=EXCLUDED.object_type, collision=EXCLUDED.collision, size_x=EXCLUDED.size_x, size_y=EXCLUDED.size_y, action_id=EXCLUDED.action_id, required_skill=EXCLUDED.required_skill, required_level=EXCLUDED.required_level, drop_item_id=EXCLUDED.drop_item_id, drop_quantity=EXCLUDED.drop_quantity, respawn_ticks=EXCLUDED.respawn_ticks, craft_action_id=EXCLUDED.craft_action_id, examine_text=EXCLUDED.examine_text, default_clip=EXCLUDED.default_clip, looping=EXCLUDED.looping, rotation_x=EXCLUDED.rotation_x, rotation_y=EXCLUDED.rotation_y, rotation_z=EXCLUDED.rotation_z, depleted_object_id=EXCLUDED.depleted_object_id, pickable=EXCLUDED.pickable;

INSERT INTO npc_definitions (id,name,model_path,size_x,size_y,is_attackable,max_hp,attack,strength,melee_defense,ranged_defense,attack_speed_ticks,respawn_ticks,is_talkable,dialogue,ai,examine_text) VALUES
  ('chicken','Chicken','assets/models/chicken.glb',1,1,TRUE,3,1,1,1,0,16,150,FALSE,'','wander','It''s a chicken.'),
  ('shopkeeper','Shopkeeper','assets/models/npc_pawn.gltf',1,1,FALSE,10,0,0,0,0,0,0,TRUE,'','static','This is a friendly shopkeeper.')
ON CONFLICT (id) DO UPDATE SET name=EXCLUDED.name, model_path=EXCLUDED.model_path, size_x=EXCLUDED.size_x, size_y=EXCLUDED.size_y, is_attackable=EXCLUDED.is_attackable, max_hp=EXCLUDED.max_hp, attack=EXCLUDED.attack, strength=EXCLUDED.strength, melee_defense=EXCLUDED.melee_defense, ranged_defense=EXCLUDED.ranged_defense, attack_speed_ticks=EXCLUDED.attack_speed_ticks, respawn_ticks=EXCLUDED.respawn_ticks, is_talkable=EXCLUDED.is_talkable, dialogue=EXCLUDED.dialogue, ai=EXCLUDED.ai, examine_text=EXCLUDED.examine_text;

INSERT INTO npc_drops (npc_id,item_id,quantity,rate) VALUES
  ('chicken','egg',1,1)
ON CONFLICT (npc_id, item_id) DO UPDATE SET quantity=EXCLUDED.quantity, rate=EXCLUDED.rate;

