#ifdef TOOLS_ENABLED

#include "ai_api_doc_loader.h"

// Returns true if p_word appears as a standalone token in p_haystack.
// Simple boundary check: char before/after must not be alphanumeric or _.
bool AIAPIDocLoader::_word_in_string(const String &p_haystack, const String &p_word) {
	int pos = p_haystack.find(p_word);
	while (pos >= 0) {
		bool left_ok  = (pos == 0) || !is_unicode_identifier_continue(p_haystack[pos - 1]);
		bool right_ok = (pos + p_word.length() >= (int)p_haystack.length()) ||
		                !is_unicode_identifier_continue(p_haystack[pos + p_word.length()]);
		if (left_ok && right_ok) {
			return true;
		}
		pos = p_haystack.find(p_word, pos + 1);
	}
	return false;
}

const HashMap<StringName, String> &AIAPIDocLoader::_get_class_docs() {
	static HashMap<StringName, String> docs;
	if (!docs.is_empty()) {
		return docs;
	}

	// --- Core Motion ---
	docs["CharacterBody3D"] =
		"CharacterBody3D: velocity:Vector3, move_and_slide()->bool, is_on_floor()->bool, is_on_wall()->bool, is_on_ceiling()->bool.\n"
		"  motion_mode: MOTION_MODE_GROUNDED(default,platformer) | MOTION_MODE_FLOATING(top-down/vehicles/boats — required for non-platformer).\n"
		"  up_direction:Vector3=Vector3.UP, floor_snap_length:float, floor_max_angle:float, wall_min_slide_angle:float.\n"
		"  CRITICAL: For top-down or vehicle games, always set motion_mode = CharacterBody3D.MOTION_MODE_FLOATING.\n";

	docs["CharacterBody2D"] =
		"CharacterBody2D: velocity:Vector2, move_and_slide()->bool, is_on_floor()->bool, is_on_wall()->bool.\n"
		"  motion_mode: MOTION_MODE_GROUNDED(default) | MOTION_MODE_FLOATING.\n"
		"  up_direction:Vector2=Vector2.UP, floor_snap_length:float.\n";

	docs["RigidBody3D"] =
		"RigidBody3D: linear_velocity:Vector3, angular_velocity:Vector3, mass:float, gravity_scale:float.\n"
		"  apply_central_impulse(impulse:Vector3), apply_impulse(impulse:Vector3, pos:Vector3=Vector3.ZERO).\n"
		"  apply_central_force(force), apply_force(force, pos). freeze:bool, freeze_mode:FreezeModeEnum.\n"
		"  Signals: body_entered(body:Node), body_exited(body:Node) — only if contact_monitor=true and max_contacts_reported>0.\n"
		"  IMPORTANT: Do NOT put body_entered on RigidBody3D itself; use an Area3D child for overlap detection instead.\n";

	docs["RigidBody2D"] =
		"RigidBody2D: linear_velocity:Vector2, angular_velocity:float, mass:float, gravity_scale:float.\n"
		"  apply_central_impulse(impulse:Vector2), apply_impulse(impulse:Vector2, pos:Vector2=Vector2.ZERO).\n"
		"  freeze:bool, contact_monitor:bool, max_contacts_reported:int.\n"
		"  Signals: body_entered, body_exited (requires contact_monitor=true).\n";

	// --- Camera ---
	docs["Camera3D"] =
		"Camera3D: fov:float=75, near:float, far:float, environment:Environment, attributes:CameraAttributes.\n"
		"  make_current() — use this to activate camera (NOT current=true).\n"
		"  is_current()->bool, get_viewport()->Viewport.\n"
		"  project_ray_origin(screen_point)->Vector3, project_ray_normal(screen_point)->Vector3.\n";

	docs["Camera2D"] =
		"Camera2D: offset:Vector2, zoom:Vector2=Vector2(1,1), limit_left/right/top/bottom:int.\n"
		"  position_smoothing_enabled:bool, position_smoothing_speed:float.\n"
		"  drag_horizontal_enabled:bool, drag_vertical_enabled:bool.\n"
		"  make_current() — CRITICAL: no 'current' property. Call make_current() AFTER add_child().\n"
		"  is_current()->bool.\n";

	// --- Navigation ---
	docs["NavigationAgent3D"] =
		"NavigationAgent3D: target_position:Vector3 (NOT destination), path_desired_distance:float, target_desired_distance:float.\n"
		"  is_navigation_finished()->bool, get_next_path_position()->Vector3, distance_to_target()->float.\n"
		"  navigation_map:RID, path_postprocessing, avoidance_enabled:bool, velocity:Vector3 (set for avoidance).\n"
		"  Signals: navigation_finished, target_reached, velocity_computed(safe_velocity:Vector3).\n"
		"  Usage: set target_position each frame or on arrival; call get_next_path_position() in _physics_process.\n";

	docs["NavigationAgent2D"] =
		"NavigationAgent2D: target_position:Vector2, path_desired_distance:float, target_desired_distance:float.\n"
		"  is_navigation_finished()->bool, get_next_path_position()->Vector2.\n"
		"  Signals: navigation_finished, target_reached, velocity_computed(safe_velocity:Vector2).\n";

	docs["NavigationRegion3D"] =
		"NavigationRegion3D: navigation_mesh:NavigationMesh, enabled:bool.\n"
		"  bake_navigation_mesh(on_thread:bool=true) — bakes navmesh at runtime.\n"
		"  navigation_mesh_changed signal.\n";

	// --- Animation ---
	docs["AnimationPlayer"] =
		"AnimationPlayer: current_animation:StringName (read/write to play; was String pre-4.6), speed_scale:float, autoplay:StringName.\n"
		"  play(name:StringName='', custom_blend:float=-1, custom_speed:float=1.0, from_end:bool=false).\n"
		"  play_backwards(name), stop(keep_state:bool=false), pause().\n"
		"  has_animation(name)->bool, get_animation_list()->PackedStringArray.\n"
		"  current_animation_position:float, current_animation_length:float.\n"
		"  Signals: animation_finished(anim_name:StringName), animation_changed(old_name, new_name).\n"
		"  NOTE (4.6+): current_animation/assigned_animation/autoplay are StringName; GDScript auto-converts from String.\n";

	docs["AnimationTree"] =
		"AnimationTree: tree_root:AnimationRootNode, anim_player:NodePath, active:bool.\n"
		"  Read/write state machine param: set('parameters/StateMachine/current', 'state_name').\n"
		"  Read/write blend param: set('parameters/BlendSpace2D/blend_position', Vector2(x,y)).\n"
		"  Read/write 1D blend: set('parameters/BlendSpace1D/blend_position', 0.5).\n"
		"  Transition request: set('parameters/StateMachine/transition_request', 'target_state').\n"
		"  IMPORTANT: parameter paths use 'parameters/' prefix + node name + '/current' or '/blend_position'.\n";

	// --- TileMap ---
	docs["TileMapLayer"] =
		"TileMapLayer (Godot 4.3+, replaces TileMap): tile_set:TileSet, enabled:bool, modulate:Color.\n"
		"  set_cell(coords:Vector2i, source_id:int, atlas_coords:Vector2i=Vector2i(-1,-1), alt_tile:int=0).\n"
		"  erase_cell(coords:Vector2i) — same as set_cell with source_id=-1.\n"
		"  get_cell_source_id(coords)->int (-1 if empty), get_cell_atlas_coords(coords)->Vector2i.\n"
		"  get_used_cells()->Array[Vector2i], get_used_rect()->Rect2i.\n"
		"  local_to_map(local_pos:Vector2)->Vector2i, map_to_local(map_pos:Vector2i)->Vector2.\n"
		"  For batch fill: loop over coords array and call set_cell() per cell.\n";

	// --- Particles ---
	docs["GPUParticles3D"] =
		"GPUParticles3D: emitting:bool, amount:int, lifetime:float, one_shot:bool, preprocess:float.\n"
		"  process_material:Material (usually ParticleProcessMaterial), draw_pass_1:Mesh.\n"
		"  restart() — restart emission. finished signal (only when one_shot=true).\n"
		"  visibility_aabb:AABB — must cover full path of particles or they disappear.\n"
		"  ParticleProcessMaterial: emission_shape, direction, spread, gravity, initial_velocity_min/max,\n"
		"    scale_min/max, color, color_ramp:GradientTexture1D, attractor_interaction_enabled.\n";

	docs["GPUParticles2D"] =
		"GPUParticles2D: emitting:bool, amount:int, lifetime:float, one_shot:bool.\n"
		"  process_material:ParticleProcessMaterial, texture:Texture2D.\n"
		"  restart(). finished signal (one_shot only).\n";

	// --- Audio ---
	docs["AudioStreamPlayer"] =
		"AudioStreamPlayer: stream:AudioStream, volume_db:float=0, pitch_scale:float=1, autoplay:bool, bus:StringName='Master'.\n"
		"  play(from_position:float=0.0), stop(), seek(pos:float).\n"
		"  playing:bool (read-only), get_playback_position()->float.\n"
		"  Signals: finished.\n";

	docs["AudioStreamPlayer2D"] =
		"AudioStreamPlayer2D: stream:AudioStream, volume_db:float, pitch_scale:float, max_distance:float, attenuation:float, bus:StringName.\n"
		"  play(from_position:float=0.0), stop(). playing:bool (read-only). Signals: finished.\n";

	docs["AudioStreamPlayer3D"] =
		"AudioStreamPlayer3D: stream:AudioStream, volume_db:float, unit_size:float, max_distance:float, bus:StringName.\n"
		"  play(from_position:float=0.0), stop(). playing:bool (read-only). Signals: finished.\n";

	// --- Shader / Material ---
	docs["ShaderMaterial"] =
		"ShaderMaterial: shader:Shader.\n"
		"  set_shader_parameter(param:StringName, value:Variant) — set uniform values.\n"
		"  get_shader_parameter(param:StringName)->Variant.\n"
		"  IMPORTANT: param name must exactly match the uniform name in the .gdshader file.\n";

	docs["StandardMaterial3D"] =
		"StandardMaterial3D: albedo_color:Color, albedo_texture:Texture2D, metallic:float, roughness:float.\n"
		"  emission_enabled:bool, emission:Color, emission_energy_multiplier:float.\n"
		"  transparency: TRANSPARENCY_DISABLED|ALPHA|ALPHA_SCISSOR|ALPHA_HASH|ALPHA_DEPTH_PRE_PASS.\n"
		"  shading_mode: SHADING_MODE_UNSHADED|PER_VERTEX|PER_PIXEL.\n"
		"  cull_mode: CULL_BACK|FRONT|DISABLED. no_depth_test:bool, render_priority:int.\n"
		"  CAUTION: no_depth_test=true + transparency=ALPHA = invisible in forward_plus. Use UNSHADED instead.\n";

	// --- Physics shapes ---
	docs["Area3D"] =
		"Area3D: collision_layer:int (bitmask), collision_mask:int (bitmask), monitoring:bool, monitorable:bool.\n"
		"  gravity_space_override:SpaceOverrideMode, gravity:float, gravity_direction:Vector3.\n"
		"  Signals: body_entered(body:Node3D), body_exited, area_entered(area:Area3D), area_exited.\n"
		"  get_overlapping_bodies()->Array[Node3D], get_overlapping_areas()->Array[Area3D].\n"
		"  IMPORTANT: collision_layer/mask are BITMASKS. Layer 1=1, Layer 2=2, Layer 3=4, Layer 4=8.\n"
		"  IMPORTANT: Use call_deferred() for add_child/remove_child inside body_entered callbacks.\n";

	docs["Area2D"] =
		"Area2D: collision_layer:int (bitmask), collision_mask:int (bitmask), monitoring:bool, monitorable:bool.\n"
		"  Signals: body_entered(body:Node2D), body_exited, area_entered, area_exited.\n"
		"  get_overlapping_bodies()->Array[Node2D].\n"
		"  IMPORTANT: collision_layer/mask are BITMASKS. Use call_deferred() inside signal callbacks.\n";

	docs["CollisionShape3D"] =
		"CollisionShape3D: shape:Shape3D (BoxShape3D/SphereShape3D/CapsuleShape3D/etc.), disabled:bool.\n"
		"  CAUTION: BoxShape3D snags on trimesh edges. Use CapsuleShape3D for characters/vehicles.\n"
		"  set_deferred('disabled', true/false) when changing inside physics callbacks.\n";

	docs["CollisionShape2D"] =
		"CollisionShape2D: shape:Shape2D (RectangleShape2D/CircleShape2D/CapsuleShape2D/etc.), disabled:bool.\n"
		"  TIP: Make shape slightly smaller than tile (e.g. 48px in a 64px grid) for smooth movement.\n";

	docs["RayCast3D"] =
		"RayCast3D: enabled:bool, target_position:Vector3, collision_mask:int (bitmask).\n"
		"  is_colliding()->bool, get_collider()->Object, get_collision_point()->Vector3, get_collision_normal()->Vector3.\n"
		"  force_raycast_update() — force update when not in physics frame. exclude_parent:bool.\n";

	docs["RayCast2D"] =
		"RayCast2D: enabled:bool, target_position:Vector2, collision_mask:int (bitmask).\n"
		"  is_colliding()->bool, get_collider()->Object, get_collision_point()->Vector2.\n"
		"  force_raycast_update().\n";

	// --- Tween ---
	docs["Tween"] =
		"Tween: created via node.create_tween() — NOT Tween.new().\n"
		"  tween_property(obj:Object, prop:NodePath, final_val:Variant, dur:float)->PropertyTweener.\n"
		"  tween_method(method:Callable, from, to, dur)->MethodTweener.\n"
		"  tween_callback(callback:Callable)->CallbackTweener.\n"
		"  tween_interval(time:float)->IntervalTweener.\n"
		"  set_loops(loops:int=0), set_parallel(parallel:bool), set_ease(ease), set_trans(trans).\n"
		"  Signals: finished, step_finished(idx:int), loop_finished(loop_count:int).\n"
		"  Chain: tween_property(...).set_trans(Tween.TRANS_BOUNCE).set_ease(Tween.EASE_OUT).\n";

	// --- Timer ---
	docs["Timer"] =
		"Timer: wait_time:float, one_shot:bool, autostart:bool, paused:bool.\n"
		"  start(time_sec:float=-1) — pass -1 to use wait_time. stop(). time_left:float (read-only).\n"
		"  is_stopped()->bool. Signals: timeout.\n"
		"  IMPORTANT: Timer is a Node — add it to the scene tree with add_child().\n";

	// --- GridMap ---
	docs["GridMap"] =
		"GridMap: mesh_library:MeshLibrary, cell_size:Vector3, cell_octant_size:int.\n"
		"  set_cell_item(position:Vector3i, item:int, orientation:int=0).\n"
		"  get_cell_item(position:Vector3i)->int (-1 if empty).\n"
		"  get_used_cells()->Array[Vector3i], get_used_cells_by_item(item:int)->Array[Vector3i].\n"
		"  ITEM_INVALID=-1 (use to erase). map_to_local(pos:Vector3i)->Vector3, local_to_map(pos)->Vector3i.\n";

	// --- MultiMesh ---
	docs["MultiMesh"] =
		"MultiMesh: mesh:Mesh, instance_count:int, transform_format:TRANSFORM_3D|2D.\n"
		"  set_instance_transform(instance:int, transform:Transform3D).\n"
		"  set_instance_color(instance:int, color:Color) — requires use_colors=true.\n"
		"  visible_instance_count:int (-1 = all). instance_count must be set BEFORE set_instance_transform.\n";

	docs["MultiMeshInstance3D"] =
		"MultiMeshInstance3D: multimesh:MultiMesh resource.\n"
		"  IMPORTANT: NO set_surface_override_material() — set material on MultiMesh.mesh instead.\n"
		"  For custom AABB (if particles fly far): set_custom_aabb(AABB(origin, size)).\n";

	// --- Path ---
	docs["PathFollow3D"] =
		"PathFollow3D (child of Path3D): progress:float (meters along path), progress_ratio:float (0.0-1.0).\n"
		"  loop:bool, cubic_interp:bool, rotation_mode:RotationMode.\n"
		"  Animate progress_ratio with Tween or AnimationPlayer to move along path.\n";

	docs["PathFollow2D"] =
		"PathFollow2D (child of Path2D): progress:float, progress_ratio:float (0.0-1.0). loop:bool.\n";

	// --- Skeleton ---
	docs["Skeleton3D"] =
		"Skeleton3D: get_bone_count()->int, find_bone(name:String)->int (-1 if not found).\n"
		"  get_bone_pose(bone_idx:int)->Transform3D, set_bone_pose(bone_idx:int, pose:Transform3D).\n"
		"  get_bone_pose_position(bone_idx)->Vector3, set_bone_pose_position(bone_idx, pos:Vector3).\n"
		"  get_bone_pose_rotation(bone_idx)->Quaternion, get_bone_global_pose(bone_idx)->Transform3D.\n";

	// --- SubViewport ---
	docs["SubViewport"] =
		"SubViewport: size:Vector2i, render_target_update_mode:UpdateMode.\n"
		"  UpdateMode: UPDATE_DISABLED|ONCE|WHEN_VISIBLE(default)|WHEN_PARENT_VISIBLE|ALWAYS.\n"
		"  render_target_clear_mode, transparent_bg:bool.\n"
		"  get_texture()->ViewportTexture — use as texture on MeshInstance3D or TextureRect.\n";

	// --- JSON / FileAccess ---
	docs["JSON"] =
		"JSON (static class in GDScript): JSON.stringify(data:Variant)->String, JSON.parse_string(str:String)->Variant.\n"
		"  Also: var j = JSON.new(); j.parse(str:String)->Error; j.data->Variant.\n"
		"  IMPORTANT: JSON.parse_string() returns null on parse failure (not an error object).\n";

	docs["FileAccess"] =
		"FileAccess (static open): FileAccess.open(path, mode)->FileAccess (null if fails).\n"
		"  Modes: READ, WRITE, READ_WRITE, WRITE_READ.\n"
		"  file_exists(path:String)->bool (static). get_as_text()->String, store_string(str), store_line(str).\n"
		"  get_line()->String, get_length()->int, eof_reached()->bool. close() — always close after use.\n";

	docs["DirAccess"] =
		"DirAccess: DirAccess.open(path)->DirAccess. make_dir(path), make_dir_recursive(path).\n"
		"  DirAccess.make_dir_recursive_absolute(path:String)->Error (static, preferred).\n"
		"  get_files()->PackedStringArray, get_directories()->PackedStringArray. list_dir_begin(), get_next()->String.\n"
		"  remove(path:String)->Error (static: DirAccess.remove_absolute(path)).\n";

	// --- Scene tree / Node ---
	docs["SceneTree"] =
		"SceneTree: get_nodes_in_group(group:StringName)->Array[Node].\n"
		"  call_group(group:StringName, method:StringName, ...) — broadcasts method call.\n"
		"  change_scene_to_file(path:String)->Error, change_scene_to_packed(packed:PackedScene)->Error.\n"
		"  paused:bool, quit(exit_code:int=0), create_tween()->Tween.\n"
		"  process_frame signal (emitted each frame), physics_frame signal.\n";

	docs["Node"] =
		"Node: name:StringName, owner:Node, process_mode:ProcessMode.\n"
		"  add_child(node:Node, force_readable_name:bool=false), remove_child(node:Node).\n"
		"  get_node(path:NodePath)->Node, get_node_or_null(path)->Node (safe), find_child(pattern)->Node.\n"
		"  queue_free() — deferred free (safe in callbacks). free() — immediate (dangerous in callbacks).\n"
		"  add_to_group(group:StringName, persistent:bool=false), remove_from_group, is_in_group->bool.\n"
		"  call_deferred(method:StringName, ...) — deferred call (safe for physics callbacks).\n"
		"  set_deferred(property:StringName, value) — deferred assignment.\n"
		"  IMPORTANT: set_owner(root) MUST come AFTER add_child(), never before.\n";

	docs["Node3D"] =
		"Node3D: position:Vector3, rotation:Vector3 (radians), rotation_degrees:Vector3, scale:Vector3.\n"
		"  global_position:Vector3, global_rotation:Vector3, global_transform:Transform3D.\n"
		"  look_at(target:Vector3, up:Vector3=Vector3.UP, use_model_front:bool=false).\n"
		"  translate(offset:Vector3), rotate(axis:Vector3, angle:float).\n"
		"  visible:bool, show(), hide().\n";

	docs["Node2D"] =
		"Node2D: position:Vector2, rotation:float (radians), rotation_degrees:float, scale:Vector2.\n"
		"  global_position:Vector2, global_rotation:float, global_transform:Transform2D.\n"
		"  look_at(point:Vector2). visible:bool, show(), hide().\n";

	// --- WorldEnvironment ---
	docs["WorldEnvironment"] =
		"WorldEnvironment: environment:Environment, camera_attributes:CameraAttributes.\n"
		"  Environment: background_mode, sky:Sky, ambient_light_color, glow_enabled, ssao_enabled, fog_enabled.\n"
		"  Sky: sky_material:SkyMaterial (ProceduralSkyMaterial/PanoramaSkyMaterial/PhysicalSkyMaterial).\n"
		"  ProceduralSkyMaterial: sky_top_color, sky_horizon_color, ground_bottom_color.\n"
		"  Multiple DirectionalLight3D: set sky_mode = SKY_MODE_LIGHT_ONLY on fill lights to avoid multiple sun discs.\n";

	docs["DirectionalLight3D"] =
		"DirectionalLight3D: light_color:Color, light_energy:float, shadow_enabled:bool.\n"
		"  sky_mode: SKY_MODE_LIGHT_AND_SKY(default) | SKY_MODE_LIGHT_ONLY | SKY_MODE_SKY_ONLY.\n"
		"  IMPORTANT: Multiple DirectionalLight3D with SKY_MODE_LIGHT_AND_SKY = multiple sun discs in sky.\n"
		"  Fix: set all fill lights to sky_mode = DirectionalLight3D.SKY_MODE_LIGHT_ONLY.\n";

	// --- HTTPRequest ---
	docs["HTTPRequest"] =
		"HTTPRequest (Node): request(url:String, headers:PackedStringArray=[], method:HTTPClient.Method=GET, body:String='')->Error.\n"
		"  request_raw(url, headers, method, body:PackedByteArray)->Error.\n"
		"  Signals: request_completed(result:int, response_code:int, headers:PackedStringArray, body:PackedByteArray).\n"
		"  result codes: RESULT_SUCCESS=0, RESULT_CANT_CONNECT=1, RESULT_NO_RESPONSE=5, etc.\n"
		"  Must be added to scene tree (add_child). max_redirects:int, timeout:float.\n";

	// --- Control / UI ---
	docs["Control"] =
		"Control: position:Vector2, size:Vector2, custom_minimum_size:Vector2, pivot_offset:Vector2.\n"
		"  anchor_left/right/top/bottom:float (0.0-1.0). set_anchors_preset(preset:LayoutPreset).\n"
		"  PRESET_FULL_RECT, PRESET_TOP_LEFT, PRESET_CENTER, PRESET_WIDE.\n"
		"  size_flags_horizontal/vertical: SIZE_FILL|SIZE_EXPAND|SIZE_SHRINK_CENTER|SIZE_EXPAND_FILL.\n"
		"  add_theme_color_override(name, color), add_theme_font_size_override(name, size).\n"
		"  add_theme_constant_override(name, value), add_theme_stylebox_override(name, stylebox).\n"
		"  IMPORTANT: For FULL_RECT anchors, set_deferred('size', value) instead of direct assignment in _ready().\n";

	docs["Button"] =
		"Button: text:String, icon:Texture2D, disabled:bool, toggle_mode:bool, button_pressed:bool.\n"
		"  flat:bool, alignment:HorizontalAlignment, expand_icon:bool.\n"
		"  Signals: pressed(), button_up(), button_down(), toggled(button_pressed:bool).\n";

	docs["LineEdit"] =
		"LineEdit: text:String, placeholder_text:String, max_length:int, secret:bool (password), editable:bool.\n"
		"  clear(), select_all(), select(from, to), deselect().\n"
		"  Signals: text_submitted(new_text:String), text_changed(new_text:String), focus_entered, focus_exited.\n";

	docs["RichTextLabel"] =
		"RichTextLabel: bbcode_enabled:bool, text:String (plain), fit_content:bool, scroll_active:bool.\n"
		"  append_text(bbcode:String), add_text(text:String), clear(), newline().\n"
		"  set_use_bbcode(true) for bbcode. push_color(color), pop(). [b], [i], [url], [img] tags supported.\n";

	docs["Label"] =
		"Label: text:String, horizontal_alignment:HorizontalAlignment, vertical_alignment:VerticalAlignment.\n"
		"  autowrap_mode:AutowrapMode — CRITICAL: NOT a bool. Use TextServer.AUTOWRAP_WORD_SMART (most common),\n"
		"    AUTOWRAP_ARBITRARY, AUTOWRAP_WORD, or AUTOWRAP_OFF. WRONG: label.autowrap=true. CORRECT: label.autowrap_mode=TextServer.AUTOWRAP_WORD_SMART.\n"
		"  clip_text:bool, text_overrun_behavior:OverrunBehavior, max_lines_visible:int.\n"
		"  add_theme_font_size_override('font_size', 24), add_theme_color_override('font_color', Color.WHITE).\n";

	// TileMap is deprecated since 4.3 — steer AI toward TileMapLayer.
	docs["TileMap"] =
		"TileMap: DEPRECATED since Godot 4.3. Use TileMapLayer instead (one node per layer).\n"
		"  If you see TileMap in existing code: each layer maps to a separate TileMapLayer node.\n"
		"  Migrate: replace TileMap.set_cell(layer, coords, ...) with TileMapLayer.set_cell(coords, ...).\n";

	return docs;
}

String AIAPIDocLoader::get_docs_for_message(const String &p_message) {
	const HashMap<StringName, String> &docs = _get_class_docs();

	// Gather matched class docs.
	String result;
	for (const KeyValue<StringName, String> &kv : docs) {
		if (_word_in_string(p_message, String(kv.key))) {
			result += kv.value;
		}
	}

	if (result.is_empty()) {
		return String();
	}

	return "\n## API Reference (auto-injected for classes mentioned in your message)\n" + result + "\n";
}

#endif // TOOLS_ENABLED
