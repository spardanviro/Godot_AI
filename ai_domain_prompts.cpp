#ifdef TOOLS_ENABLED

#include "ai_domain_prompts.h"

// ---------------------------------------------------------------------------
// Keyword tables for each domain
// ---------------------------------------------------------------------------

static const char *kw_animation[] = {
	"animation", "animationtree", "animationplayer", "statemachine",
	"blendspace", "blend_tree", "blend tree", "blend space",
	"keyframe", "tween",
	// Chinese
	"\xe5\x8a\xa8\xe7\x94\xbb",       // 动画
	"\xe7\x8a\xb6\xe6\x80\x81\xe6\x9c\xba", // 状态机
	"\xe6\xb7\xb7\xe5\x90\x88",       // 混合
	"\xe5\x85\xb3\xe9\x94\xae\xe5\xb8\xa7", // 关键帧
	nullptr
};

static const char *kw_tilemap[] = {
	"tilemap", "tileset", "tile_map", "tile_set", "tile",
	"\xe7\x93\xa6\xe7\x89\x87", // 瓦片
	"\xe5\x9c\xb0\xe5\x9b\xbe\xe5\x9d\x97", // 地图块
	nullptr
};

static const char *kw_navigation[] = {
	"navigation", "navmesh", "pathfind", "path_find",
	"navigationregion", "navigationagent", "a_star", "astar",
	"\xe5\xaf\xbc\xe8\x88\xaa", // 导航
	"\xe5\xaf\xbb\xe8\xb7\xaf", // 寻路
	nullptr
};

static const char *kw_particles[] = {
	"particle", "gpuparticle", "cpuparticle", "emitter", "emission",
	"\xe7\xb2\x92\xe5\xad\x90", // 粒子
	"\xe5\x8f\x91\xe5\xb0\x84\xe5\x99\xa8", // 发射器
	nullptr
};

static const char *kw_shader[] = {
	"shader", "gdshader", "shadermaterial", "uniform",
	"vertex", "fragment",
	"\xe7\x9d\x80\xe8\x89\xb2\xe5\x99\xa8", // 着色器
	nullptr
};

static const char *kw_physics[] = {
	"rigidbody", "staticbody", "characterbody", "collision",
	"raycast", "joint", "hinge", "collision_layer", "collision_mask",
	"\xe7\x89\xa9\xe7\x90\x86", // 物理
	"\xe7\xa2\xb0\xe6\x92\x9e", // 碰撞
	"\xe5\x88\x9a\xe4\xbd\x93", // 刚体
	"\xe5\xb0\x84\xe7\xba\xbf", // 射线
	nullptr
};

static const char *kw_batch[] = {
	"batch", "bulk", "cross-scene", "all scene",
	"\xe6\x89\xb9\xe9\x87\x8f", // 批量
	"\xe8\xb7\xa8\xe5\x9c\xba\xe6\x99\xaf", // 跨场景
	nullptr
};

static const char *kw_audio[] = {
	"audio", "sound", "music", "audiobus", "audiostreamplayer",
	"\xe9\x9f\xb3\xe9\xa2\x91", // 音频
	"\xe9\x9f\xb3\xe4\xb9\x90", // 音乐
	"\xe5\xa3\xb0\xe9\x9f\xb3", // 声音
	"\xe9\x9f\xb3\xe6\x95\x88", // 音效
	nullptr
};

static const char *kw_3d_scene[] = {
	"mesh", "meshinstance", "camera3d", "light3d",
	"directionallight", "omnilight", "spotlight",
	"environment", "worldenvironment", "gridmap",
	"\xe7\xbd\x91\xe6\xa0\xbc", // 网格
	"\xe7\x81\xaf\xe5\x85\x89", // 灯光
	"\xe7\x8e\xaf\xe5\xa2\x83", // 环境
	nullptr
};

static const char *kw_screenshot[] = {
	"screenshot", "capture", "viewport_image",
	"\xe6\x88\xaa\xe5\x9b\xbe", // 截图
	"\xe5\xb1\x8f\xe5\xb9\x95\xe6\x88\xaa\xe5\x8f\x96", // 屏幕截取
	nullptr
};

static const char *kw_game_ui[] = {
	// HUD / overlay
	"hud", "health bar", "healthbar", "hp bar", "hpbar", "mana bar", "manabar",
	"stamina bar", "staminabar", "resource bar",
	// Skill / cooldown
	"skill bar", "skillbar", "cooldown", "skill slot",
	// Inventory
	"inventory", "item slot", "itemslot", "item grid",
	// Dialog / conversation
	"dialog box", "dialogue box", "dialog system", "typewriter",
	// Minimap
	"minimap", "mini map",
	// Game UI general
	"game ui", "game hud", "overlay ui", "floating text", "damage number",
	"buff icon", "status effect", "canvaslayer", "canvas layer",
	// Chinese
	"\xe8\xa1\x80\xe9\x87\x8f\xe6\xa1\x86",   // 血量框
	"\xe8\xa1\x80\xe6\xa1\xa5",                // 血槽
	"\xe8\xa1\x80\xe9\x87\x8f\xe6\x9d\xa1",   // 血量条
	"\xe6\x8a\x80\xe8\x83\xbd\xe6\xa0\x8f",   // 技能栏
	"\xe6\x8a\x80\xe8\x83\xbd\xe5\x86\xb7\xe5\x8d\xb4", // 技能冷却
	"\xe8\x83\x8c\xe5\x8c\x85",               // 背包
	"\xe7\x89\xa9\xe5\x93\x81\xe6\xa0\xbc",   // 物品格
	"\xe5\xaf\xb9\xe8\xaf\x9d\xe6\xa1\xa5",   // 对话框
	"\xe6\xb5\xae\xe5\x8a\xa8\xe6\x96\x87\xe5\xad\x97", // 浮动文字
	"\xe4\xbc\xa4\xe5\xae\xb3\xe6\x95\xb0\xe5\xad\x97", // 伤害数字
	"\xe5\xb0\x8f\xe5\x9c\xb0\xe5\x9b\xbe",  // 小地图
	"\xe6\xb8\xb8\xe6\x88\x8f\xe7\x95\x8c\xe9\x9d\xa2", // 游戏界面
	"\xe6\x8a\xac\xe5\xa4\xb4\xe6\x98\xbe\xe7\xa4\xba", // 抬头显示
	nullptr
};

static const char *kw_complex_project[] = {
	"complete game", "full game", "whole game", "entire game",
	"make a game", "build a game", "create a game",
	"complete project", "full project", "whole project",
	"rpg", "platformer", "shooter", "racing game", "tower defense",
	// Chinese
	"\xe5\xae\x8c\xe6\x95\xb4\xe6\xb8\xb8\xe6\x88\x8f", // 完整游戏
	"\xe5\xae\x8c\xe6\x95\xb4\xe9\xa1\xb9\xe7\x9b\xae", // 完整项目
	"\xe5\x88\xb6\xe4\xbd\x9c\xe6\xb8\xb8\xe6\x88\x8f", // 制作游戏
	"\xe5\x88\x9b\xe5\xbb\xba\xe6\xb8\xb8\xe6\x88\x8f", // 创建游戏
	"\xe5\x8a\xa8\xe4\xbd\x9c\xe6\xb8\xb8\xe6\x88\x8f", // 动作游戏
	"\xe5\xb9\xb3\xe5\x8f\xb0\xe6\xb8\xb8\xe6\x88\x8f", // 平台游戏
	"\xe5\xa1\x94\xe9\x98\xb2\xe5\xbe\xa1", // 塔防
	nullptr
};

// ---------------------------------------------------------------------------
// detect_domains
// ---------------------------------------------------------------------------

bool AIDomainPrompts::_matches_keywords(const String &p_lower, const char *const *p_keywords) {
	for (int i = 0; p_keywords[i]; i++) {
		if (p_lower.find(String::utf8(p_keywords[i])) != -1) {
			return true;
		}
	}
	return false;
}

Vector<AIDomainPrompts::Domain> AIDomainPrompts::detect_domains(const String &p_message) {
	Vector<Domain> result;
	String lower = p_message.to_lower();

	struct { const char *const *keywords; Domain domain; } table[] = {
		{ kw_animation,       DOMAIN_ANIMATION },
		{ kw_tilemap,         DOMAIN_TILEMAP },
		{ kw_navigation,      DOMAIN_NAVIGATION },
		{ kw_particles,       DOMAIN_PARTICLES },
		{ kw_shader,          DOMAIN_SHADER },
		{ kw_physics,         DOMAIN_PHYSICS },
		{ kw_batch,           DOMAIN_BATCH },
		{ kw_audio,           DOMAIN_AUDIO },
		{ kw_3d_scene,        DOMAIN_3D_SCENE },
		{ kw_screenshot,      DOMAIN_SCREENSHOT },
		{ kw_complex_project, DOMAIN_COMPLEX_PROJECT },
		{ kw_game_ui,         DOMAIN_GAME_UI },
	};

	for (int i = 0; i < 12; i++) {
		if (_matches_keywords(lower, table[i].keywords)) {
			result.push_back(table[i].domain);
		}
	}

	return result;
}

// ---------------------------------------------------------------------------
// Domain prompt fragments
// ---------------------------------------------------------------------------

static String _prompt_animation() {
	String p;
	p += "\n## Animation System Specialization\n\n";

	p += "### AnimationPlayer\n";
	p += "```gdscript\n";
	p += "var anim_player = AnimationPlayer.new()\n";
	p += "anim_player.name = \"AnimationPlayer\"\n";
	p += "parent.add_child(anim_player)\n";
	p += "anim_player.owner = scene_root\n";
	p += "var anim = Animation.new()\n";
	p += "anim.length = 1.0\n";
	p += "anim.loop_mode = Animation.LOOP_LINEAR\n";
	p += "# Property track: animate a node's property\n";
	p += "var track_idx = anim.add_track(Animation.TYPE_VALUE)\n";
	p += "anim.track_set_path(track_idx, \"Sprite2D:position\")\n";
	p += "anim.track_insert_key(track_idx, 0.0, Vector2(0, 0))\n";
	p += "anim.track_insert_key(track_idx, 1.0, Vector2(100, 0))\n";
	p += "var lib = AnimationLibrary.new()\n";
	p += "lib.add_animation(\"move_right\", anim)\n";
	p += "anim_player.add_animation_library(\"\", lib)\n";
	p += "```\n\n";

	p += "### AnimationTree + StateMachine\n";
	p += "```gdscript\n";
	p += "var tree = AnimationTree.new()\n";
	p += "tree.name = \"AnimationTree\"\n";
	p += "# Root is an AnimationNodeStateMachine\n";
	p += "var sm = AnimationNodeStateMachine.new()\n";
	p += "# Add states\n";
	p += "sm.add_node(\"idle\", AnimationNodeAnimation.new())\n";
	p += "sm.add_node(\"run\",  AnimationNodeAnimation.new())\n";
	p += "sm.add_node(\"jump\", AnimationNodeAnimation.new())\n";
	p += "# Set animation names\n";
	p += "sm.get_node(\"idle\").animation = \"idle\"\n";
	p += "sm.get_node(\"run\").animation  = \"run\"\n";
	p += "sm.get_node(\"jump\").animation = \"jump\"\n";
	p += "# Add transitions\n";
	p += "var t1 = AnimationNodeStateMachineTransition.new()\n";
	p += "t1.advance_mode = AnimationNodeStateMachineTransition.ADVANCE_MODE_AUTO\n";
	p += "sm.add_transition(\"idle\", \"run\", t1)\n";
	p += "var t2 = AnimationNodeStateMachineTransition.new()\n";
	p += "sm.add_transition(\"run\", \"idle\", t2)\n";
	p += "tree.tree_root = sm\n";
	p += "tree.anim_player = NodePath(\"../AnimationPlayer\")\n";
	p += "parent.add_child(tree)\n";
	p += "tree.owner = scene_root\n";
	p += "```\n\n";

	p += "### BlendSpace2D\n";
	p += "```gdscript\n";
	p += "var bs = AnimationNodeBlendSpace2D.new()\n";
	p += "var anim_idle = AnimationNodeAnimation.new()\n";
	p += "anim_idle.animation = \"idle\"\n";
	p += "bs.add_blend_point(anim_idle, Vector2(0, 0))\n";
	p += "var anim_run = AnimationNodeAnimation.new()\n";
	p += "anim_run.animation = \"run\"\n";
	p += "bs.add_blend_point(anim_run, Vector2(1, 0))\n";
	p += "bs.min_space = Vector2(-1, -1)\n";
	p += "bs.max_space = Vector2(1, 1)\n";
	p += "```\n\n";

	p += "### Tween (procedural animation)\n";
	p += "```gdscript\n";
	p += "# In game scripts, NOT in EditorScript:\n";
	p += "var tween = create_tween()\n";
	p += "tween.tween_property(sprite, \"position\", Vector2(200, 0), 0.5)\n";
	p += "tween.set_trans(Tween.TRANS_QUAD).set_ease(Tween.EASE_OUT)\n";
	p += "tween.tween_callback(func(): print(\"done\"))\n";
	p += "```\n";

	return p;
}

static String _prompt_tilemap() {
	String p;
	p += "\n## TileMap Specialization\n\n";

	p += "### Create TileMap with TileSet\n";
	p += "```gdscript\n";
	p += "var tilemap = TileMapLayer.new()  # Godot 4.3+ uses TileMapLayer\n";
	p += "tilemap.name = \"Ground\"\n";
	p += "var tileset = TileSet.new()\n";
	p += "tileset.tile_size = Vector2i(16, 16)\n";
	p += "# Add a source (atlas)\n";
	p += "var source = TileSetAtlasSource.new()\n";
	p += "source.texture = load(\"res://textures/tileset.png\")\n";
	p += "source.texture_region_size = Vector2i(16, 16)\n";
	p += "tileset.add_source(source, 0)  # source_id = 0\n";
	p += "tilemap.tile_set = tileset\n";
	p += "```\n\n";

	p += "### Batch Fill Rectangle\n";
	p += "```gdscript\n";
	p += "# Fill a rectangular area with one tile\n";
	p += "var source_id: int = 0\n";
	p += "var atlas_coords := Vector2i(0, 0)\n";
	p += "for x in range(start_x, start_x + width):\n";
	p += "\tfor y in range(start_y, start_y + height):\n";
	p += "\t\ttilemap.set_cell(Vector2i(x, y), source_id, atlas_coords)\n";
	p += "```\n\n";

	p += "### Query Used Cells\n";
	p += "```gdscript\n";
	p += "var used_cells: Array[Vector2i] = tilemap.get_used_cells()\n";
	p += "var rect: Rect2i = tilemap.get_used_rect()\n";
	p += "# Clear all cells\n";
	p += "tilemap.clear()\n";
	p += "```\n\n";

	p += "IMPORTANT: In Godot 4.3+, TileMap is deprecated. Use TileMapLayer instead. Each TileMapLayer represents one layer.\n";

	return p;
}

static String _prompt_navigation() {
	String p;
	p += "\n## Navigation System Specialization\n\n";

	p += "### 2D Navigation Setup\n";
	p += "```gdscript\n";
	p += "# Create NavigationRegion2D\n";
	p += "var nav_region = NavigationRegion2D.new()\n";
	p += "nav_region.name = \"NavigationRegion2D\"\n";
	p += "var nav_poly = NavigationPolygon.new()\n";
	p += "nav_poly.add_outline(PackedVector2Array([Vector2(0,0), Vector2(500,0), Vector2(500,500), Vector2(0,500)]))\n";
	p += "nav_poly.make_polygons_from_outlines()\n";
	p += "nav_region.navigation_polygon = nav_poly\n";
	p += "parent.add_child(nav_region)\n";
	p += "nav_region.owner = scene_root\n";
	p += "```\n\n";

	p += "### 3D Navigation Setup\n";
	p += "```gdscript\n";
	p += "var nav_region = NavigationRegion3D.new()\n";
	p += "nav_region.name = \"NavigationRegion3D\"\n";
	p += "var nav_mesh = NavigationMesh.new()\n";
	p += "nav_mesh.agent_radius = 0.5\n";
	p += "nav_mesh.agent_height = 1.8\n";
	p += "nav_region.navigation_mesh = nav_mesh\n";
	p += "parent.add_child(nav_region)\n";
	p += "nav_region.owner = scene_root\n";
	p += "# Bake at runtime:\n";
	p += "# nav_region.bake_navigation_mesh()\n";
	p += "```\n\n";

	p += "### NavigationAgent (in game scripts)\n";
	p += "```gdscript\n";
	p += "# Attach NavigationAgent2D to a CharacterBody2D\n";
	p += "var agent = NavigationAgent2D.new()\n";
	p += "agent.name = \"NavigationAgent2D\"\n";
	p += "agent.path_desired_distance = 4.0\n";
	p += "agent.target_desired_distance = 4.0\n";
	p += "character.add_child(agent)\n";
	p += "agent.owner = scene_root\n";
	p += "# In the game script's _physics_process:\n";
	p += "# agent.target_position = target_pos\n";
	p += "# var next = agent.get_next_path_position()\n";
	p += "# velocity = position.direction_to(next) * speed\n";
	p += "# move_and_slide()\n";
	p += "```\n\n";

	p += "### Calculate Path (runtime)\n";
	p += "```gdscript\n";
	p += "var path: PackedVector2Array = NavigationServer2D.map_get_path(\n";
	p += "\tget_world_2d().navigation_map, start_pos, end_pos, true)\n";
	p += "```\n";

	return p;
}

static String _prompt_particles() {
	String p;
	p += "\n## Particle System Specialization\n\n";

	p += "### GPUParticles2D Setup\n";
	p += "```gdscript\n";
	p += "var particles = GPUParticles2D.new()\n";
	p += "particles.name = \"Sparks\"\n";
	p += "particles.amount = 50\n";
	p += "particles.lifetime = 1.0\n";
	p += "particles.emitting = true\n";
	p += "var mat = ParticleProcessMaterial.new()\n";
	p += "mat.direction = Vector3(0, -1, 0)\n";
	p += "mat.spread = 30.0\n";
	p += "mat.initial_velocity_min = 50.0\n";
	p += "mat.initial_velocity_max = 100.0\n";
	p += "mat.gravity = Vector3(0, 98, 0)\n";
	p += "mat.scale_min = 0.5\n";
	p += "mat.scale_max = 1.5\n";
	p += "# Color ramp\n";
	p += "var gradient = Gradient.new()\n";
	p += "gradient.set_color(0, Color(1, 0.8, 0, 1))\n";
	p += "gradient.add_point(0.5, Color(1, 0.3, 0, 1))\n";
	p += "gradient.set_color(1, Color(0.3, 0, 0, 0))\n";
	p += "var grad_tex = GradientTexture1D.new()\n";
	p += "grad_tex.gradient = gradient\n";
	p += "mat.color_ramp = grad_tex\n";
	p += "particles.process_material = mat\n";
	p += "```\n\n";

	p += "### Emission Shapes\n";
	p += "```gdscript\n";
	p += "# Point (default), Sphere, Box, Ring\n";
	p += "mat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE\n";
	p += "mat.emission_sphere_radius = 20.0\n";
	p += "# Box:\n";
	p += "mat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_BOX\n";
	p += "mat.emission_box_extents = Vector3(50, 10, 0)\n";
	p += "```\n\n";

	p += "### 3D Particles\n";
	p += "Use GPUParticles3D with the same ParticleProcessMaterial. Add a mesh to `draw_pass_1`:\n";
	p += "```gdscript\n";
	p += "var particles3d = GPUParticles3D.new()\n";
	p += "particles3d.draw_pass_1 = SphereMesh.new()  # or QuadMesh\n";
	p += "particles3d.process_material = mat\n";
	p += "```\n";

	return p;
}

static String _prompt_shader() {
	String p;
	p += "\n## Shader Specialization\n\n";

	p += "### Create Shader File\n";
	p += "```gdscript\n";
	p += "DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(\"res://shaders/\"))\n";
	p += "var f = FileAccess.open(\"res://shaders/dissolve.gdshader\", FileAccess.WRITE)\n";
	p += "f.store_line(\"shader_type canvas_item;\")\n";
	p += "f.store_line(\"\")\n";
	p += "f.store_line(\"uniform float progress : hint_range(0.0, 1.0) = 0.0;\")\n";
	p += "f.store_line(\"uniform sampler2D noise_tex;\")\n";
	p += "f.store_line(\"\")\n";
	p += "f.store_line(\"void fragment() {\")\n";
	p += "f.store_line(\"    float noise = texture(noise_tex, UV).r;\")\n";
	p += "f.store_line(\"    if (noise < progress) { discard; }\")\n";
	p += "f.store_line(\"}\")\n";
	p += "f.close()\n";
	p += "get_editor_interface().get_resource_filesystem().scan()\n";
	p += "```\n\n";

	p += "### Apply ShaderMaterial to a Node\n";
	p += "```gdscript\n";
	p += "var shader = load(\"res://shaders/dissolve.gdshader\")\n";
	p += "var mat = ShaderMaterial.new()\n";
	p += "mat.shader = shader\n";
	p += "mat.set_shader_parameter(\"progress\", 0.0)\n";
	p += "sprite.material = mat\n";
	p += "```\n\n";

	p += "### Shader Types\n";
	p += "- `shader_type spatial;` — 3D meshes\n";
	p += "- `shader_type canvas_item;` — 2D sprites, UI\n";
	p += "- `shader_type particles;` — particle process\n";
	p += "- `shader_type sky;` — sky rendering\n";
	p += "- `shader_type fog;` — volumetric fog\n";

	return p;
}

static String _prompt_physics() {
	String p;
	p += "\n## Physics System Specialization\n\n";

	p += "### Collision Shape Setup\n";
	p += "```gdscript\n";
	p += "# 2D: Add collision to a body\n";
	p += "var col = CollisionShape2D.new()\n";
	p += "col.name = \"CollisionShape2D\"\n";
	p += "var shape = RectangleShape2D.new()\n";
	p += "shape.size = Vector2(32, 32)\n";
	p += "col.shape = shape\n";
	p += "body.add_child(col)\n";
	p += "col.owner = scene_root\n";
	p += "\n";
	p += "# 3D: BoxShape3D, SphereShape3D, CapsuleShape3D, CylinderShape3D\n";
	p += "var col3d = CollisionShape3D.new()\n";
	p += "var box = BoxShape3D.new()\n";
	p += "box.size = Vector3(1, 2, 1)\n";
	p += "col3d.shape = box\n";
	p += "```\n\n";

	p += "### Collision Layers & Masks\n";
	p += "```gdscript\n";
	p += "# Layers: what this object IS on\n";
	p += "# Masks: what this object DETECTS\n";
	p += "body.collision_layer = 1  # Layer 1 (bit 0)\n";
	p += "body.collision_mask = 2   # Detects layer 2 (bit 1)\n";
	p += "# Use set_collision_layer_value / set_collision_mask_value for named layers:\n";
	p += "body.set_collision_layer_value(1, true)  # Enable layer 1\n";
	p += "body.set_collision_mask_value(2, true)   # Detect layer 2\n";
	p += "```\n\n";

	p += "### RayCast (in game scripts)\n";
	p += "```gdscript\n";
	p += "var ray = RayCast2D.new()\n";
	p += "ray.target_position = Vector2(0, 50)  # Cast downward 50px\n";
	p += "ray.collision_mask = 1  # Only detect layer 1\n";
	p += "ray.enabled = true\n";
	p += "# Query: ray.is_colliding(), ray.get_collider(), ray.get_collision_point()\n";
	p += "```\n\n";

	p += "### Physics Joints (2D)\n";
	p += "```gdscript\n";
	p += "var pin = PinJoint2D.new()\n";
	p += "pin.node_a = body_a.get_path()\n";
	p += "pin.node_b = body_b.get_path()\n";
	p += "pin.position = body_a.position  # Pivot point\n";
	p += "```\n\n";

	p += "### RigidBody Forces\n";
	p += "```gdscript\n";
	p += "# In game scripts:\n";
	p += "# rigid_body.apply_central_impulse(Vector2(0, -300))  # Jump\n";
	p += "# rigid_body.apply_force(Vector2(100, 0))             # Continuous\n";
	p += "# rigid_body.linear_velocity = Vector2(0, 0)          # Stop\n";
	p += "```\n";

	return p;
}

static String _prompt_batch() {
	String p;
	p += "\n## Cross-Scene Batch Operations Specialization\n\n";

	p += "### Iterate All Scene Files\n";
	p += "```gdscript\n";
	p += "var efs = get_editor_interface().get_resource_filesystem()\n";
	p += "var scenes: Array[String] = []\n";
	p += "var stack: Array[String] = [\"res://\"]\n";
	p += "while stack.size() > 0:\n";
	p += "\tvar dir_path: String = stack.pop_back()\n";
	p += "\tvar dir = DirAccess.open(dir_path)\n";
	p += "\tif not dir:\n";
	p += "\t\tcontinue\n";
	p += "\tdir.list_dir_begin()\n";
	p += "\tvar file_name: String = dir.get_next()\n";
	p += "\twhile file_name != \"\":\n";
	p += "\t\tvar full_path: String = dir_path.path_join(file_name)\n";
	p += "\t\tif dir.current_is_dir() and not file_name.begins_with(\".\"):\n";
	p += "\t\t\tstack.push_back(full_path)\n";
	p += "\t\telif file_name.ends_with(\".tscn\") or file_name.ends_with(\".scn\"):\n";
	p += "\t\t\tscenes.push_back(full_path)\n";
	p += "\t\tfile_name = dir.get_next()\n";
	p += "\tdir.list_dir_end()\n";
	p += "print(\"Found \", scenes.size(), \" scenes\")\n";
	p += "```\n\n";

	p += "### Batch Modify Nodes Across Scenes\n";
	p += "```gdscript\n";
	p += "for scene_path in scenes:\n";
	p += "\tvar packed: PackedScene = load(scene_path)\n";
	p += "\tvar instance: Node = packed.instantiate()\n";
	p += "\t# Find nodes by type or name\n";
	p += "\tvar targets: Array[Node] = _find_all(instance, \"Light2D\")\n";
	p += "\tfor node in targets:\n";
	p += "\t\tnode.set(\"energy\", 1.5)  # Modify property\n";
	p += "\t# Re-pack and save\n";
	p += "\tvar new_packed = PackedScene.new()\n";
	p += "\tnew_packed.pack(instance)\n";
	p += "\tResourceSaver.save(new_packed, scene_path)\n";
	p += "\tinstance.queue_free()\n";
	p += "\n";
	p += "func _find_all(root: Node, type_name: String) -> Array[Node]:\n";
	p += "\tvar result: Array[Node] = []\n";
	p += "\tif root.is_class(type_name):\n";
	p += "\t\tresult.push_back(root)\n";
	p += "\tfor child in root.get_children():\n";
	p += "\t\tresult.append_array(_find_all(child, type_name))\n";
	p += "\treturn result\n";
	p += "```\n\n";

	p += "### Bulk Find by Group\n";
	p += "```gdscript\n";
	p += "var scene_root = get_scene()\n";
	p += "var enemies = scene_root.get_tree().get_nodes_in_group(\"enemies\")\n";
	p += "for enemy in enemies:\n";
	p += "\tenemy.set(\"health\", 100)\n";
	p += "```\n";

	return p;
}

static String _prompt_audio() {
	String p;
	p += "\n## Audio System Specialization\n\n";

	p += "### AudioStreamPlayer Setup\n";
	p += "```gdscript\n";
	p += "# 2D positional audio\n";
	p += "var player = AudioStreamPlayer2D.new()\n";
	p += "player.name = \"SFXPlayer\"\n";
	p += "player.stream = load(\"res://audio/sfx/explosion.wav\")\n";
	p += "player.volume_db = -6.0\n";
	p += "player.max_distance = 500.0\n";
	p += "parent.add_child(player)\n";
	p += "player.owner = scene_root\n";
	p += "\n";
	p += "# Non-positional (BGM)\n";
	p += "var bgm = AudioStreamPlayer.new()\n";
	p += "bgm.name = \"BGMPlayer\"\n";
	p += "bgm.stream = load(\"res://audio/music/theme.ogg\")\n";
	p += "bgm.bus = \"Music\"  # Route to Music bus\n";
	p += "bgm.autoplay = true\n";
	p += "```\n\n";

	p += "### Audio Bus Management (in EditorScript)\n";
	p += "```gdscript\n";
	p += "# Godot manages buses via AudioServer at runtime.\n";
	p += "# In EditorScript, create default_bus_layout.tres:\n";
	p += "var layout = AudioBusLayout.new()\n";
	p += "# Bus 0 is always Master. Add new buses:\n";
	p += "AudioServer.add_bus()        # Bus 1\n";
	p += "AudioServer.set_bus_name(1, \"Music\")\n";
	p += "AudioServer.set_bus_volume_db(1, -3.0)\n";
	p += "AudioServer.add_bus()        # Bus 2\n";
	p += "AudioServer.set_bus_name(2, \"SFX\")\n";
	p += "# Add effects:\n";
	p += "var reverb = AudioEffectReverb.new()\n";
	p += "AudioServer.add_bus_effect(1, reverb)\n";
	p += "```\n";

	return p;
}

static String _prompt_3d_scene() {
	String p;
	p += "\n## 3D Scene Setup Specialization\n\n";

	p += "### Complete 3D Scene\n";
	p += "```gdscript\n";
	p += "var scene_root = get_scene()\n";
	p += "var ur = get_editor_interface().get_editor_undo_redo()\n";
	p += "\n";
	p += "# Camera\n";
	p += "var cam = Camera3D.new()\n";
	p += "cam.name = \"MainCamera\"\n";
	p += "cam.position = Vector3(0, 5, 10)\n";
	p += "cam.rotation_degrees = Vector3(-25, 0, 0)\n";
	p += "\n";
	p += "# Directional Light\n";
	p += "var light = DirectionalLight3D.new()\n";
	p += "light.name = \"Sun\"\n";
	p += "light.rotation_degrees = Vector3(-45, -30, 0)\n";
	p += "light.shadow_enabled = true\n";
	p += "\n";
	p += "# Environment\n";
	p += "var env_node = WorldEnvironment.new()\n";
	p += "env_node.name = \"WorldEnvironment\"\n";
	p += "var env = Environment.new()\n";
	p += "env.background_mode = Environment.BG_SKY\n";
	p += "var sky = Sky.new()\n";
	p += "sky.sky_material = ProceduralSkyMaterial.new()\n";
	p += "env.sky = sky\n";
	p += "env.tonemap_mode = Environment.TONE_MAP_ACES\n";
	p += "env.ssao_enabled = true\n";
	p += "env_node.environment = env\n";
	p += "\n";
	p += "# Floor mesh\n";
	p += "var floor_inst = MeshInstance3D.new()\n";
	p += "floor_inst.name = \"Floor\"\n";
	p += "var plane = PlaneMesh.new()\n";
	p += "plane.size = Vector2(20, 20)\n";
	p += "floor_inst.mesh = plane\n";
	p += "var floor_mat = StandardMaterial3D.new()\n";
	p += "floor_mat.albedo_color = Color(0.4, 0.4, 0.4)\n";
	p += "floor_inst.set_surface_override_material(0, floor_mat)\n";
	p += "# Static body for collision\n";
	p += "var sb = StaticBody3D.new()\n";
	p += "sb.name = \"FloorBody\"\n";
	p += "var cs = CollisionShape3D.new()\n";
	p += "var box_shape = BoxShape3D.new()\n";
	p += "box_shape.size = Vector3(20, 0.1, 20)\n";
	p += "cs.shape = box_shape\n";
	p += "\n";
	p += "for node in [cam, light, env_node, floor_inst, sb]:\n";
	p += "\tur.create_action(\"Add \" + node.name)\n";
	p += "\tur.add_do_method(scene_root, \"add_child\", node)\n";
	p += "\tur.add_do_property(node, \"owner\", scene_root)\n";
	p += "\tur.add_do_reference(node)\n";
	p += "\tur.add_undo_method(scene_root, \"remove_child\", node)\n";
	p += "\tur.commit_action()\n";
	p += "sb.add_child(cs)\n";
	p += "cs.owner = scene_root\n";
	p += "```\n\n";

	p += "### GridMap (3D Tile-based levels)\n";
	p += "```gdscript\n";
	p += "var gridmap = GridMap.new()\n";
	p += "gridmap.name = \"GridMap\"\n";
	p += "gridmap.cell_size = Vector3(2, 2, 2)\n";
	p += "# Assign a MeshLibrary resource\n";
	p += "gridmap.mesh_library = load(\"res://resources/mesh_library.tres\")\n";
	p += "# Place items: set_cell_item(position, item_index, orientation)\n";
	p += "gridmap.set_cell_item(Vector3i(0, 0, 0), 0)\n";
	p += "gridmap.set_cell_item(Vector3i(1, 0, 0), 0)\n";
	p += "```\n";

	return p;
}

static String _prompt_screenshot() {
	String p;
	p += "\n## Viewport Screenshot (EditorScript)\n\n";
	p += "```gdscript\n";
	p += "# Capture the editor's 3D viewport\n";
	p += "var viewport = EditorInterface.get_editor_viewport_3d(0)\n";
	p += "if viewport:\n";
	p += "\tvar img: Image = viewport.get_texture().get_image()\n";
	p += "\tDirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(\"res://screenshots/\"))\n";
	p += "\timg.save_png(\"res://screenshots/capture.png\")\n";
	p += "\tprint(\"Screenshot saved.\")\n";
	p += "\n";
	p += "# Capture the 2D viewport\n";
	p += "var vp2d = EditorInterface.get_editor_viewport_2d()\n";
	p += "if vp2d:\n";
	p += "\tvar img2d: Image = vp2d.get_texture().get_image()\n";
	p += "\timg2d.save_png(\"res://screenshots/capture_2d.png\")\n";
	p += "```\n";

	return p;
}

static String _prompt_complex_project() {
	String p;
	p += "\n## Risk-First Decomposition (MANDATORY for full game / complete project requests)\n\n";

	p += "When building a COMPLETE GAME or FULL PROJECT, follow risk-first decomposition:\n\n";

	p += "### Step 1 — Identify High-Risk Components\n";
	p += "Before writing any code, identify which features carry the highest implementation risk.\n";
	p += "ISOLATE these first (prototype separately before integrating):\n";
	p += "- **Procedural generation** (dungeon gen, terrain, L-systems) — algorithm correctness is hard to debug in a full scene\n";
	p += "- **Custom animations / animation trees** — StateMachine transitions, BlendSpace2D blending\n";
	p += "- **Vehicle / ragdoll / cloth physics** — RigidBody joints, spring arms, wheel colliders\n";
	p += "- **Custom shaders** — vertex deformation, screen-space effects, geometry shaders\n";
	p += "- **Runtime geometry** — Mesh built from SurfaceTool/ArrayMesh, MeshDataTool\n";
	p += "- **Dynamic navigation** — runtime navmesh baking, NavigationAgent avoidance\n";
	p += "- **Complex camera rigs** — spring arm + lerp + look-at chains (notorious for frame-1 origin glitch)\n\n";

	p += "NEVER isolate these (they are standard, low-risk):\n";
	p += "- CharacterBody2D/3D movement with move_and_slide()\n";
	p += "- TileMapLayer placement and queries\n";
	p += "- NavigationAgent on a static navmesh\n";
	p += "- Standard UI (Control containers, Labels, Buttons)\n";
	p += "- Spawning / pooling via instantiate()\n";
	p += "- Timer-based events\n\n";

	p += "### Step 2 — Build in Order\n";
	p += "1. **Core loop first**: movement, collision, camera — verify the scene runs without errors\n";
	p += "2. **Content systems**: enemies, items, levels — build on top of working core\n";
	p += "3. **Polish**: particles, shaders, sound, UI — add last\n\n";

	p += "### Step 3 — Verification Criteria\n";
	p += "Before declaring a feature 'done', verify:\n";
	p += "- [ ] No parse errors (script loads without error)\n";
	p += "- [ ] No runtime errors in first 30 seconds of play\n";
	p += "- [ ] Camera starts at correct position (no origin-swoop on frame 1)\n";
	p += "- [ ] Physics collision works (player lands on floor, doesn't fall through)\n";
	p += "- [ ] Input actions are registered in InputMap before use\n\n";

	p += "### Common Full-Game Architecture\n";
	p += "```\n";
	p += "res://\n";
	p += "├── scenes/\n";
	p += "│   ├── game/        game.tscn (root: Node), game.gd (game loop)\n";
	p += "│   ├── player/      player.tscn, player_controller.gd\n";
	p += "│   ├── enemies/     enemy_base.tscn, enemy_base.gd\n";
	p += "│   ├── ui/          hud.tscn, hud.gd, main_menu.tscn\n";
	p += "│   └── levels/      level_01.tscn\n";
	p += "├── scripts/\n";
	p += "│   └── autoloads/   game_manager.gd, audio_manager.gd\n";
	p += "├── resources/\n";
	p += "│   └── items/       sword.tres, potion.tres\n";
	p += "└── assets/\n";
	p += "    ├── textures/\n";
	p += "    └── audio/\n";
	p += "```\n\n";

	p += "### Signal Architecture for Multi-Script Games\n";
	p += "Root node coordinates — children emit signals upward:\n";
	p += "- Player emits: `player_died`, `score_changed(delta:int)`, `health_changed(hp:int)`\n";
	p += "- Enemy emits: `enemy_died(position:Vector3)`, `damage_dealt(amount:int)`\n";
	p += "- GameManager listens to all, updates HUD, triggers level transitions\n";
	p += "- NEVER have enemies directly reference the Player node — use groups or signals\n\n";

	return p;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Game UI Domain prompt
// ---------------------------------------------------------------------------

static String _prompt_game_ui() {
	String p;
	p += "\n## Game UI Specialization (Godot 4 Control System)\n\n";

	p += "### Unity vs Godot UI mapping\n";
	p += "- Canvas (Overlay) → CanvasLayer (layer = 5 for HUD, 10 for menus, 100 for effects)\n";
	p += "- Canvas (World Space) → Label3D with billboard = BILLBOARD_ENABLED\n";
	p += "- RectTransform anchors → set_anchors_preset() or anchor_left/right/top/bottom\n";
	p += "- Image → ColorRect (solid color) / TextureRect (texture) / Panel (styled)\n";
	p += "- TextMeshPro → Label / RichTextLabel (bbcode_enabled = true for formatting)\n";
	p += "- VerticalLayoutGroup → VBoxContainer\n";
	p += "- HorizontalLayoutGroup → HBoxContainer\n";
	p += "- GridLayoutGroup → GridContainer (set .columns)\n";
	p += "- ScrollRect → ScrollContainer\n";
	p += "- CanvasScaler → Project Settings > Display > Window > Stretch\n\n";

	p += "### Anchor presets — always use these instead of manual anchor values\n";
	p += "```gdscript\n";
	p += "control.set_anchors_preset(Control.PRESET_TOP_LEFT)      # HP bar\n";
	p += "control.set_anchors_preset(Control.PRESET_TOP_RIGHT)     # minimap\n";
	p += "control.set_anchors_preset(Control.PRESET_BOTTOM_LEFT)   # chat log\n";
	p += "control.set_anchors_preset(Control.PRESET_BOTTOM_RIGHT)  # action buttons\n";
	p += "control.set_anchors_preset(Control.PRESET_CENTER_BOTTOM) # skill bar\n";
	p += "control.set_anchors_preset(Control.PRESET_CENTER)        # crosshair, dialog\n";
	p += "control.set_anchors_preset(Control.PRESET_FULL_RECT)     # fullscreen bg/effect\n";
	p += "# After preset, set pixel offsets:\n";
	p += "control.offset_left = 20; control.offset_top = 20  # margin from anchor\n";
	p += "```\n\n";

	p += "### CanvasLayer layering\n";
	p += "```gdscript\n";
	p += "var hud_layer = CanvasLayer.new()\n";
	p += "hud_layer.layer = 5    # game HUD\n";
	p += "var menu_layer = CanvasLayer.new()\n";
	p += "menu_layer.layer = 10  # pause / main menu\n";
	p += "var fx_layer = CanvasLayer.new()\n";
	p += "fx_layer.layer = 100   # damage flash, vignette\n";
	p += "```\n\n";

	p += "### Health bar — correct pattern\n";
	p += "Use ColorRect for fill bars (NOT ProgressBar unless you need dragging).\n";
	p += "The fill ColorRect width = parent.size.x * (current / max).\n";
	p += "```gdscript\n";
	p += "# Scene structure: HBoxContainer > [HpLabel, BarContainer > [BG, DelayedFill, Fill], ValueLabel]\n";
	p += "var bar_container = Control.new()\n";
	p += "bar_container.custom_minimum_size = Vector2(180, 20)\n";
	p += "bar_container.size_flags_horizontal = Control.SIZE_EXPAND_FILL\n";
	p += "var bg = ColorRect.new(); bg.set_anchors_preset(Control.PRESET_FULL_RECT)\n";
	p += "bg.color = Color(0.1, 0.1, 0.1, 0.85)\n";
	p += "var delayed = ColorRect.new(); delayed.set_anchors_preset(Control.PRESET_FULL_RECT)\n";
	p += "delayed.color = Color(0.8, 0.2, 0.2)  # red, lags behind\n";
	p += "var fill = ColorRect.new(); fill.set_anchors_preset(Control.PRESET_FULL_RECT)\n";
	p += "fill.color = Color(0.2, 0.8, 0.2)     # green, current HP\n";
	p += "# In runtime script: fill.size.x = bar_container.size.x * ratio\n";
	p += "```\n\n";

	p += "### Skill slot with radial cooldown\n";
	p += "```gdscript\n";
	p += "# Panel (64x64) > [TextureRect icon, TextureProgressBar overlay, Label cd_label, Label key_hint]\n";
	p += "var slot = Panel.new(); slot.custom_minimum_size = Vector2(64, 64)\n";
	p += "var icon = TextureRect.new()\n";
	p += "icon.set_anchors_preset(Control.PRESET_FULL_RECT)\n";
	p += "icon.expand_mode = TextureRect.EXPAND_FIT_WIDTH_PROPORTIONAL\n";
	p += "var overlay = TextureProgressBar.new()\n";
	p += "overlay.set_anchors_preset(Control.PRESET_FULL_RECT)\n";
	p += "overlay.fill_mode = TextureProgressBar.FILL_CLOCKWISE\n";
	p += "overlay.tint_progress = Color(0, 0, 0, 0.75)\n";
	p += "overlay.value = 0.0; overlay.max_value = 1.0\n";
	p += "overlay.mouse_filter = Control.MOUSE_FILTER_IGNORE\n";
	p += "# Runtime: overlay.value = cooldown_remaining / cooldown_duration\n";
	p += "```\n\n";

	p += "### Inventory grid\n";
	p += "```gdscript\n";
	p += "# ScrollContainer > GridContainer (columns=6)\n";
	p += "var scroll = ScrollContainer.new()\n";
	p += "scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED\n";
	p += "var grid = GridContainer.new(); grid.columns = 6\n";
	p += "grid.add_theme_constant_override(\"h_separation\", 4)\n";
	p += "grid.add_theme_constant_override(\"v_separation\", 4)\n";
	p += "# Each item slot: Panel (64x64) > [ColorRect bg, TextureRect icon, Label stack_count]\n";
	p += "# Rarity colors: common=gray, uncommon=green, rare=blue, epic=purple, legendary=orange\n";
	p += "```\n\n";

	p += "### Floating damage numbers — use Tween, NOT _process\n";
	p += "```gdscript\n";
	p += "# In a damage_number.gd script attached to a Node2D with a Label child:\n";
	p += "func setup(damage: int, is_crit: bool = false) -> void:\n";
	p += "\t$Label.text = str(damage)\n";
	p += "\tif is_crit: $Label.add_theme_font_size_override(\"font_size\", 36)\n";
	p += "\tvar tween: Tween = create_tween().set_parallel()\n";
	p += "\ttween.tween_property(self, \"position\", position + Vector2(randf_range(-20,20), -80), 1.2).set_ease(Tween.EASE_OUT)\n";
	p += "\ttween.tween_property($Label, \"modulate:a\", 0.0, 1.2).set_delay(0.5)\n";
	p += "\ttween.chain().tween_callback(queue_free)\n";
	p += "```\n\n";

	p += "### Dialog typewriter effect — use Timer/Signal, NOT yield\n";
	p += "```gdscript\n";
	p += "# RichTextLabel with bbcode_enabled = true supports [color], [b], [wave] etc.\n";
	p += "# Use visible_characters property for typewriter effect:\n";
	p += "func show_dialog(text: String) -> void:\n";
	p += "\t$RichTextLabel.text = text\n";
	p += "\t$RichTextLabel.visible_characters = 0\n";
	p += "\tvar tween = create_tween()\n";
	p += "\ttween.tween_property($RichTextLabel, \"visible_characters\", len(text), len(text) / 30.0)\n";
	p += "```\n\n";

	p += "### StyleBox — for panel/button styling\n";
	p += "```gdscript\n";
	p += "var style = StyleBoxFlat.new()\n";
	p += "style.bg_color = Color(0.05, 0.05, 0.1, 0.92)\n";
	p += "style.corner_radius_top_left    = 8\n";
	p += "style.corner_radius_top_right   = 8\n";
	p += "style.corner_radius_bottom_left = 8\n";
	p += "style.corner_radius_bottom_right = 8\n";
	p += "style.border_color = Color(0.4, 0.4, 0.6)\n";
	p += "style.set_border_width_all(1)\n";
	p += "panel.add_theme_stylebox_override(\"panel\", style)\n";
	p += "```\n\n";

	p += "### Common mistakes to avoid\n";
	p += "- NEVER set control.size in _ready() — use custom_minimum_size or set_deferred(\"size\", ...)\n";
	p += "- NEVER add UI nodes directly to game world nodes — always use CanvasLayer\n";
	p += "- NEVER use control.position for responsive layout — use anchors_preset + offsets\n";
	p += "- RichTextLabel: always set bbcode_enabled = true before using BBCode tags\n";
	p += "- TextureProgressBar FILL_CLOCKWISE: value 0 = empty, 1 = full (invert for cooldown)\n";
	p += "- Safe area (notch): use DisplayServer.get_display_safe_area() to get margins\n";

	return p;
}

// ---------------------------------------------------------------------------
// get_domain_prompt
// ---------------------------------------------------------------------------

String AIDomainPrompts::get_domain_prompt(Domain p_domain) {
	switch (p_domain) {
		case DOMAIN_ANIMATION:       return _prompt_animation();
		case DOMAIN_TILEMAP:         return _prompt_tilemap();
		case DOMAIN_NAVIGATION:      return _prompt_navigation();
		case DOMAIN_PARTICLES:       return _prompt_particles();
		case DOMAIN_SHADER:          return _prompt_shader();
		case DOMAIN_PHYSICS:         return _prompt_physics();
		case DOMAIN_BATCH:           return _prompt_batch();
		case DOMAIN_AUDIO:           return _prompt_audio();
		case DOMAIN_3D_SCENE:        return _prompt_3d_scene();
		case DOMAIN_SCREENSHOT:      return _prompt_screenshot();
		case DOMAIN_COMPLEX_PROJECT: return _prompt_complex_project();
		case DOMAIN_GAME_UI:         return _prompt_game_ui();
		default:                     return "";
	}
}

// ---------------------------------------------------------------------------
// build_contextual_prompt
// ---------------------------------------------------------------------------

String AIDomainPrompts::build_contextual_prompt(const String &p_message) {
	Vector<Domain> domains = detect_domains(p_message);
	if (domains.is_empty()) {
		return "";
	}

	String result;
	for (int i = 0; i < domains.size(); i++) {
		result += get_domain_prompt(domains[i]);
	}
	return result;
}

#endif // TOOLS_ENABLED
