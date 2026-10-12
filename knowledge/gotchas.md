<!-- keywords: CharacterBody2D, CharacterBody3D, move_and_slide, velocity, delta, _physics_process -->
### move_and_slide() already includes delta — do NOT multiply velocity by delta
`CharacterBody2D.move_and_slide()` and `CharacterBody3D.move_and_slide()` handle the physics timestep internally. Do NOT pre-multiply `velocity` by `delta` before calling it (unlike `move_and_collide()`, which requires manual delta scaling). HOWEVER, gravity IS an acceleration and MUST be multiplied by `delta`:
```gdscript
func _physics_process(delta: float) -> void:
    velocity.y += gravity * delta  # gravity: multiply by delta (it's acceleration)
    move_and_slide()               # NO delta here — timestep already baked in
# WRONG: move_and_slide(velocity * delta)  ← double-applies timestep
```


<!-- keywords: RigidBody, velocity, RigidBody2D, RigidBody3D, physics, int -->
### RigidBody: never set position/linear_velocity directly — use _integrate_forces()
Setting `position`, `global_position`, `linear_velocity`, or `angular_velocity` on a `RigidBody2D`/`RigidBody3D` directly from outside `_integrate_forces()` breaks the physics simulation (tunneling, jitter, lost momentum). To move or rotate a rigid body from code:
- Use `_integrate_forces(state: PhysicsDirectBodyState3D)` override and set `state.linear_velocity`, `state.angular_velocity`, or `state.transform`
- For one-shot placement only (e.g. spawn position), `set_global_transform()` is acceptable
- NEVER call `look_at()` every frame on a RigidBody — use `_integrate_forces()` with angular_velocity instead


<!-- keywords: RigidBody, RigidBody2D, RigidBody3D, int -->
### RigidBody sleeping stops _integrate_forces() from being called
When a `RigidBody2D`/`RigidBody3D` has been at rest long enough, it goes to sleep to save CPU. While sleeping, `_integrate_forces()` is NOT called at all. If your rigid body logic requires continuous processing, either set `can_sleep = false` (performance cost) or apply a tiny continuous force/impulse to keep it awake.


<!-- keywords: Ray, physics, delta, _physics_process -->
### Ray-casting (direct_space_state) is only safe inside _physics_process()
The physics space is 'locked' between physics frames. Accessing `get_world_2d().direct_space_state` or `get_world_3d().direct_space_state` outside of `_physics_process()` causes an error. Always perform ray-casts / shape queries inside `_physics_process()`:
```gdscript
func _physics_process(delta: float) -> void:
    var space := get_world_3d().direct_space_state
    var query := PhysicsRayQueryParameters3D.create(origin, end)
    var result := space.intersect_ray(query)
```


<!-- keywords: Never, CollisionShape, collision, Node, CollisionShape2D, CollisionShape3D -->
### Never scale CollisionShape nodes — use shape size handles instead
Scaling a `CollisionShape2D` or `CollisionShape3D` node (via the Node2D/Node3D Scale property) causes incorrect collision behavior: the shape visually scales but collision detection uses wrong extents. ALWAYS resize collision shapes using the size handles in the editor or by setting the shape's size/radius properties directly:
  WRONG:  collision_shape.scale = Vector2(2, 2)   # scale property — breaks collisions
  CORRECT: (collision_shape.shape as BoxShape2D).size = Vector2(64, 32)  # shape size


<!-- keywords: JSON, Godot, Vector2, Vector3, Color, Rect2, Quaternion, serialization -->
### JSON cannot serialize Godot types: Vector2, Vector3, Color, Rect2, Quaternion
`JSON.stringify()` / `JSON.parse()` only handle bool, int, float, String, Array, and Dictionary. Godot-specific types like `Vector2`, `Vector3`, `Color`, `Rect2`, `Transform3D`, and `Quaternion` CANNOT be directly serialized. Decompose them manually:
```gdscript
# Saving
var data = { "pos_x": position.x, "pos_y": position.y }   # decompose Vector2
# Loading
position = Vector2(data["pos_x"], data["pos_y"])
# Alternative: use binary serialization (FileAccess.store_var / get_var) which handles all types
```


<!-- keywords: Inspector -->
### _init() assignment order: Inspector values always override _init()
When instantiating a scene, property values are applied in this order:
1. Default value (no setter called)
2. `_init()` runs and may assign values (setter IS called)
3. Inspector (exported property values) overwrite whatever `_init()` set (setter IS called)
This means values set in `_init()` are ALWAYS clobbered by Inspector values for `@export` properties. Use `_init()` for logic that must run before `_ready()`, but don't rely on it to set final `@export` values.


<!-- keywords: StandardMaterial3D, Alpha, material -->
### StandardMaterial3D with Alpha transparency: can't cast shadows + sorting issues
Alpha-blended `StandardMaterial3D` (transparency = TRANSPARENCY_ALPHA) has important limitations:
- Cannot cast shadows (only receives them)
- Does NOT appear in screen-space reflections or SDFGI sharp reflections
- Sorting artifacts when transparent surfaces overlap each other
- Significantly slower to render than opaque
For foliage/fences use `TRANSPARENCY_ALPHA_SCISSOR` (hard edges, casts shadows). For hair use `TRANSPARENCY_ALPHA_HASH`. Only use ALPHA for particles and VFX that truly need blending.


<!-- keywords: Autoload, scene, Node, autoload -->
### Autoload nodes persist across scene changes — they are NOT freed
Nodes added via Project Settings > Autoloads are children of the root node and persist when you call `get_tree().change_scene_to_file()`. They are NEVER freed during normal gameplay. This makes them suitable for cross-scene managers (audio, saves, UI), but means any state they hold survives scene transitions. Reset their state explicitly if needed on scene load.


<!-- keywords: Path2D, Path3D, PathFollow2D, PathFollow3D -->
### preload() requires a compile-time constant path — use load() for dynamic paths
`preload()` only accepts a LITERAL string (constant at compile time). You cannot use a variable or computed path:
  CORRECT: var scene = preload("res://scenes/player.tscn")  # literal string
  WRONG:   var scene = preload(my_path_var)                  # variable path — compile error
  CORRECT: var scene = load(my_path_var)                     # load() accepts variables


<!-- keywords: RenderingServer, physics, rendering, canvas, int -->
### RenderingServer canvas items need reset_physics_interpolation() on first frame
When creating canvas items directly via `RenderingServer` (bypassing nodes), call `RenderingServer.canvas_item_reset_physics_interpolation(rid)` on the first frame after creation. Without this, the item visually 'teleports in' from position (0,0) due to stale interpolation state.


<!-- keywords: collision, collision_layer -->
### set_collision_layer_value(layer_number, bool) — layer_number is 1-based
The convenience method `set_collision_layer_value(layer_number, value)` uses 1-based layer numbers (Layer 1 = 1, Layer 2 = 2, etc.), unlike the raw `collision_layer` bitmask where Layer 1 = bit 0 = value 1. This is cleaner than bitmask arithmetic:
  CORRECT: body.set_collision_layer_value(2, true)   # enable layer 2 cleanly
  CORRECT: body.set_collision_mask_value(1, true)    # enable mask for layer 1
  VERBOSE: body.collision_layer = 2                  # same but as raw bitmask


<!-- keywords: Shader, shader, GLSL -->
### Shader local variables are NOT initialized — always assign before use
In Godot's shading language, **local variables** (inside functions) are NOT zero-initialized. They contain whatever garbage was in memory at that address. Reading an unassigned local variable produces unpredictable visual glitches. Only `uniform` and `varying` declarations are initialized to a default value (0). Always assign a value before using a local variable:
```glsl
// WRONG: 'result' is uninitialized — garbage output
vec3 result;
if (some_condition) { result = vec3(1.0); }
ALBEDO = result;  // undefined behavior if condition was false

// CORRECT: always initialize
vec3 result = vec3(0.0);
if (some_condition) { result = vec3(1.0); }
ALBEDO = result;
```


<!-- keywords: Shader, shader, GLSL, int, float -->
### Shader: no implicit type casting — int/float/uint must be explicit
Godot's shading language (like GLSL ES 3.0) does NOT allow implicit casting between scalar types. Integer literals are signed `int` by default. This means:
```glsl
float a = 2;        // INVALID — int literal cannot implicitly cast to float
float a = 2.0;      // valid
float a = float(2); // valid
uint b = 2;         // INVALID — signed int literal cannot implicitly cast to uint
uint b = uint(2);   // valid
```


<!-- keywords: NavigationAgent, NavigationAgent2D, navigation, _ready -->
### NavigationAgent: DON'T query path in _ready() — map not synced yet
If you set `NavigationAgent2D.target_position` or call `get_next_path_position()` inside `_ready()`, the navigation map may not have finished its first synchronization. The result is an empty path and the agent immediately considers itself at the destination. Use `call_deferred()` or wait for the first `_physics_process()` tick:
```gdscript
func _ready() -> void:
    call_deferred("_set_initial_target")  # deferred, after map sync

func _set_initial_target() -> void:
    navigation_agent.target_position = target_pos
```


<!-- keywords: NavigationAgent, ONCE, physics, navigation, delta, _physics_process -->
### NavigationAgent: call get_next_path_position() ONCE per _physics_process, check is_navigation_finished() first
`get_next_path_position()` updates the agent's internal path state. Calling it more than once per physics frame, or calling it after the destination is reached, causes the agent to jitter. Always guard with `is_navigation_finished()`:
```gdscript
func _physics_process(delta: float) -> void:
    if navigation_agent.is_navigation_finished():
        return
    var next_pos := navigation_agent.get_next_path_position()  # call ONCE
    var direction := (next_pos - global_position).normalized()
    velocity = direction * speed
    move_and_slide()
```


<!-- keywords: Don, Node3D, Transform3D, Quaternion, Node, 3D, rotation -->
### Don't use Node3D.rotation for continuous 3D rotation — use Transform3D/Quaternion
The `rotation` property of `Node3D` is stored as Euler angles (XYZ order). Using Euler angles for continuous rotation or interpolation causes gimbal lock and non-linear behavior (rotating from 270° to 0° may spin the wrong direction). For 3D games:
- Use `global_transform.basis` to read orientation
- Rotate using `rotate_object_local()` or `rotate_y()` for incremental rotation
- Use `Quaternion.slerp()` for smooth rotation interpolation
- For FPS cameras: rotate Y on parent node (global yaw) + rotate X on child Camera3D (local pitch)


<!-- keywords: Control -->
### Control centering: use set_anchors_preset() or set anchor + offset pairs
To center a Control node in its parent from code, use the preset method (cleanest):
```gdscript
control.set_anchors_and_offsets_preset(Control.PRESET_CENTER)
# Or for full-rect fill:
control.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
```
Manual approach: set `anchor_left/right/top/bottom` to 0.5, then `offset_left/right/top/bottom` to ±(size/2). Negative offsets place the control above/left of the anchor point.


<!-- keywords: Audio, AudioStreamPlayer, audio -->
### Audio: linear volume ≠ dB — use linear_to_db() / db_to_linear()
`AudioStreamPlayer.volume_db` uses the decibel scale where 0 dB = maximum, and every -6 dB halves perceived loudness. Working range is typically -60 dB (near-silent) to 0 dB (max). To convert a 0.0–1.0 slider value to dB:
  volume_db = linear_to_db(slider_value)   # 1.0 → 0 dB, 0.5 → -6 dB, 0.0 → -inf
  slider_value = db_to_linear(volume_db)   # reverse
Never set volume_db above 0 — it causes clipping distortion on the master bus.


<!-- keywords: _draw, cached -->
### _draw() is cached — call queue_redraw() to trigger re-draw
`CanvasItem._draw()` is called **once** and its draw commands are cached. The function will NOT be called again automatically when your data changes. To trigger a redraw (e.g. when a variable changes), call `queue_redraw()` on the same node. A good pattern is to call it from a property setter:
```gdscript
@export var color: Color = Color.WHITE:
    set(value):
        color = value
        queue_redraw()  # trigger _draw() on next frame

func _draw() -> void:
    draw_circle(Vector2.ZERO, 32.0, color)
```


<!-- keywords: Editor, GDScript, gdscript, @tool, EditorPlugin -->
### Editor plugins: ALL helper GDScript files must have @tool
The EditorPlugin root script must be `@tool extends EditorPlugin`. But every OTHER GDScript file that the plugin loads or uses must ALSO start with `@tool`. A GDScript file without `@tool` that is used by an editor plugin acts as an **empty file** — its classes and functions are invisible to the plugin. Use `_enter_tree()` for initialization and `_exit_tree()` for cleanup (not `_ready()`).


<!-- keywords: Godot -->
### randomize() not needed in Godot 4 — random seed is automatic
Since Godot 4.0, the global random seed is automatically randomized at project start. You do NOT need to call `randomize()` in `_ready()`. Only call it explicitly if you need a specific deterministic seed:
  seed(12345)              # deterministic — same sequence every run
  seed("hello".hash())   # string-based seed
For per-object RNG, use `RandomNumberGenerator.new()` and call `.randomize()` on it to get an independent stream.


<!-- keywords: Input, input -->
### Input.get_vector() — clean 8-way movement input
Use `Input.get_vector(negative_x, positive_x, negative_y, positive_y)` to get a normalized diagonal-aware input vector for 8-way movement. It returns a Vector2 with magnitude ≤ 1.0:
```gdscript
func _physics_process(delta: float) -> void:
    var dir := Input.get_vector("left", "right", "up", "down")
    velocity = dir * speed
    move_and_slide()
```
This is cleaner and more correct than `Input.is_action_pressed()` + manual normalization.


<!-- keywords: TileMapLayer, NavigationRegion2D, navigation, pathfinding, TileMap, tile, 2D -->
### TileMapLayer built-in navigation is limited — bake to NavigationRegion2D for real games
TileMapLayer's built-in navigation has performance and quality limitations. For production use, after designing your TileMap, bake it to a `NavigationRegion2D` with an explicit `NavigationPolygon` and disable the TileMapLayer's navigation. Also note: **2D navigation meshes cannot be layered** — stacking multiple navigation meshes on the same navigation map causes merge errors that break pathfinding.


<!-- keywords: Light, Mobile, Compatibility, Mesh, mesh, rendering, light -->
### Light limits per renderer — Mobile and Compatibility cap at 8 per mesh
The number of lights that can affect a single mesh depends on the renderer:
- **Forward+**: 512 clustered elements (lights + decals + probes) in view — effectively unlimited for most scenes
- **Mobile**: max 8 OmniLights + 8 SpotLights per mesh resource (hard limit, cannot be changed)
- **Compatibility**: same 8+8 per-mesh limit (can be raised in Project Settings > Rendering > Limits > OpenGL at cost of performance)
Exceeding these limits silently drops some lights. For Mobile/Compatibility, use fewer dynamic lights and rely on baked lighting.


<!-- keywords: StandardMaterial3D, Material, material, OmniLight3D, light -->
### StandardMaterial3D emission does NOT light nearby objects by default
Setting `emission_enabled = true` and `emission_color` on a material makes the surface appear to glow, but it does NOT actually emit light onto nearby geometry unless:
- SDFGI or VoxelGI bakes the emission into GI, OR
- Screen-space indirect lighting is enabled (Forward+ only)
To make a glowing object actually light its surroundings, pair it with a small OmniLight3D at the same position.


<!-- keywords: READ, ONLY, export -->
### res:// is READ-ONLY in exported games — use user:// for all saves and writes
`res://` is read-write only inside the editor. In exported builds it is read-only. Always use `user://` for any file that your game needs to write at runtime (save games, settings, downloads):
```gdscript
# WRONG — crashes or silently fails in exports:
var f = FileAccess.open("res://save.dat", FileAccess.WRITE)
# CORRECT:
var f = FileAccess.open("user://save.dat", FileAccess.WRITE)
```
Always use forward slashes `/` as path delimiter — even on Windows.


<!-- keywords: GDScript, gdscript, int, float -->
### GDScript integer division truncates — use float to get decimals
When both operands of `/` are `int`, GDScript performs integer division (truncates toward zero):
  5 / 2 == 2    # NOT 2.5 !
  5 / 2.0 == 2.5  # OK — one float operand
  float(5) / 2 == 2.5  # OK — explicit cast
Also: `%` is integer-only. For float remainder use `fmod(a, b)`. For math-correct (always positive) modulo use `posmod(a, b)` / `fposmod(a, b)` (unlike `%`, which truncates like C).


<!-- keywords: GDScript, LEFT, Python, gdscript -->
### GDScript ** is LEFT-associative (unlike Python)
`2 ** 2 ** 3` evaluates as `(2 ** 2) ** 3 = 64`, NOT `2 ** 8 = 256` like in Python. Use parentheses to specify the intended order: `2 ** (2 ** 3)`.


<!-- keywords: GDScript, VALUE, gdscript, lambda, Callable -->
### GDScript lambdas capture variables by VALUE at creation time
Outer local variables captured by a lambda are copied at creation time — later reassignments to the outer variable do NOT update the lambda's copy:
```gdscript
var x = 42
var fn = func(): print(x)
x = 99
fn.call()  # Prints 42, not 99 !
```
Exception: pass-by-reference types (Array, Dictionary, Object) share their *contents* until the outer variable is reassigned to a different object. Also: call lambdas via `.call()` — the `()` operator on a Callable is not supported.


<!-- keywords: GDScript, await, coroutine, gdscript, error -->
### GDScript await: calling a coroutine without await causes an error
If a function uses `await` inside, it becomes a coroutine. Storing its return value without `await` raises a runtime error:
```gdscript
# WRONG:
var result = my_coroutine()  # Error: result is a GDScriptFunctionState, not the value
# CORRECT:
var result = await my_coroutine()
```
Calling without saving the result (`my_coroutine()` alone) is fine — it runs fire-and-forget.


<!-- keywords: GDScript, gdscript, enum, global -->
### GDScript named enum keys are NOT global constants
When you declare `enum State { IDLE, JUMP = 5, SHOOT }`, the keys `IDLE`, `JUMP`, `SHOOT` are **not** in global scope. You must prefix them with the enum name:
```gdscript
var s = State.JUMP   # CORRECT — prints 5
var s = JUMP         # ERROR — not found
```
Anonymous enums `enum { A, B, C }` DO register keys as global constants.


<!-- keywords: GDScript, gdscript, debug, assert -->
### GDScript: assert() is stripped in non-debug (release) builds
`assert(condition)` silently disappears in release exports. Never put code with side effects inside assert:
  assert(array.size() > 0)         # OK — pure check
  assert(spawn_enemy() != null)     # BAD — enemy NOT spawned in release!


<!-- keywords: GDScript, gdscript -->
### GDScript variable initialization order trap
Member variable initializers run top-to-bottom **before** `_init()`. `_init()` runs **before** `@export` values are set by the Inspector. So if `_init()` sets a var, the Inspector value will overwrite it afterward. Reference order also matters:
```gdscript
var _data: Dictionary = {}  # initialized first
var a: int = proxy(1)       # proxy() called; _data exists
# If _data were declared AFTER a, proxy() would find it empty
```


<!-- keywords: Camera3D, Near, Camera, camera, property, 3D -->
### 3D Z-fighting fix: increase Camera3D Near property
Z-fighting (flickering surfaces) happens when two surfaces occupy the same depth buffer value. The depth buffer is non-linear — most precision is near the camera. To fix Z-fighting: **increase `Camera3D.near`** (e.g. from 0.05 → 0.1 or higher). Also decrease `Camera3D.far` to the minimum needed. Alternatively, move the overlapping surfaces apart in world space.


<!-- keywords: Transparency, Node3D, rendering, 3D -->
### 3D Transparency sorting: by Node3D origin, not vertex — fix with render_priority / sorting_offset
Transparent objects are sorted back-to-front by the **origin of the Node3D**, not by vertex positions. This causes incorrect overlap with large or oddly-shaped transparent meshes. Fix:
- `material.render_priority = N` — higher value = drawn in front of other transparent materials
- `visual_instance.sorting_offset = N` — shifts the object's depth for sorting purposes (positive = drawn later / in front)
Alpha transparent materials also do NOT appear in SSR or SSAO (normal-roughness buffer). Prefer Alpha Scissor for foliage/fences — it sorts correctly and can cast shadows.


<!-- keywords: Euler, Camera, FPS -->
### FPS camera: rotate Y first, then X — never accumulate Euler angles
For a first-person controller, always apply Y-axis (horizontal look) rotation first, then X-axis (vertical look) in LOCAL space. Reversing the order causes the camera to tilt sideways. Also never lerp `Node3D.rotation` (Euler) for smooth 3D rotation — use `Transform3D.interpolate_with()` or `Quaternion.slerp()` instead:
```gdscript
# Correct FPS mouse look:
var yaw := 0.0   # horizontal (Y axis, global)
var pitch := 0.0 # vertical (X axis, local)
func _input(event):
    if event is InputEventMouseMotion:
        yaw -= event.relative.x * sensitivity
        pitch = clamp(pitch - event.relative.y * sensitivity, -89.0, 89.0)
        transform.basis = Basis.from_euler(Vector3(deg_to_rad(pitch), deg_to_rad(yaw), 0))
```


<!-- keywords: Collision, WHERE, WHAT, collision, physics, collision_layer, collision_mask -->
### Collision layer vs mask: layer = WHERE you are, mask = WHAT you scan
`collision_layer` = the physics layers this body EXISTS on. `collision_mask` = the physics layers this body DETECTS (scans for collisions). Both objects must have the right combination for a collision to register:
  - Object A (enemy) is on layer 2. Object B (player) scans layer 2 (mask includes bit 2).
  - Only B detects A, not vice versa unless A's mask also includes B's layer.
Use `set_collision_layer_value(N, bool)` / `set_collision_mask_value(N, bool)` (N is 1-based) to modify individual bits.


<!-- keywords: Physics, Godot, physics -->
### Physics is NOT deterministic in Godot
Godot's physics engine does not guarantee identical results across runs, devices, or frame rates. Never assume physics simulation will be exactly reproducible. Use `is_equal_approx()` for float comparisons derived from physics, and design for approximate outcomes.


<!-- keywords: GDScript, Godot, gdscript -->
### GDScript setters/getters: Godot 4 always calls them — even from same class
In Godot 4, property setters and getters are **always** invoked, even when accessed from inside the same class (with or without `self.`). This is different from Godot 3's `setget` which only triggered from outside. Inside the setter/getter itself, referring to the variable by name directly accesses the underlying value (bypasses the setter, preventing infinite recursion). But calling any OTHER function that also sets the variable WILL cause infinite recursion:
```gdscript
var x: int:
    set(value):
        x = value     # OK — direct variable access, no recursion
        set_x(value)  # BAD — calls set_x() which sets x through the setter → infinite loop
```


<!-- keywords: Node, RefCounted, queue_free -->
### Node memory: RefCounted is auto-freed, Node needs queue_free()
- `RefCounted` (and `Resource`) are freed **automatically** when refcount reaches 0
- `Node` objects stay in memory until explicitly freed: use `queue_free()` (safe, end of frame) or `free()` (immediate)
- `queue_free()` on a Node recursively frees ALL its children
- Use `weakref(obj)` to hold a reference without preventing auto-free
- Use `is_instance_valid(obj)` to check if an object has been freed before using it


<!-- keywords: Signal, signal, connect, emit -->
### Callable.bind() — add extra arguments at connection time
Use `.bind(extra_arg)` to pass additional context to a signal handler that the signal itself doesn't emit:
```gdscript
# Signal only emits (old_health, new_health), but we want character name too:
character.health_changed.connect(battle_log._on_health_changed.bind(character.name))

func _on_health_changed(old_val, new_val, char_name: String):
    # char_name comes from bind() — appended AFTER signal args
    label.text = char_name + " took damage"
```


<!-- keywords: Cannot, GDScript, gdscript -->
### Cannot override non-virtual engine methods in GDScript
Non-virtual engine methods like `get_class()`, `queue_free()`, `_to_string()` **cannot** be overridden in GDScript — attempting to will trigger the `NATIVE_METHOD_OVERRIDE` warning, which is treated as an error by default. Only methods marked `virtual` in the docs (and named with `_` prefix) can be overridden: `_ready()`, `_process()`, `_input()`, `_draw()`, etc.


<!-- keywords: AnimationTree, AnimationPlayer, animation -->
### AnimationTree requires a separate AnimationPlayer — it has no animations of its own
`AnimationTree` does NOT store animations. It blends and transitions animations from an `AnimationPlayer` node. You must: (1) create/import animations in `AnimationPlayer`, (2) add `AnimationTree` to your scene, (3) set its `anim_player` property to point to the `AnimationPlayer`. This is especially relevant for imported 3D scenes: the `AnimationPlayer` lives inside the imported scene; your own scene's `AnimationTree` references it.


<!-- keywords: StandardMaterial3D, Transparency, Material, material -->
### StandardMaterial3D: alpha blending is forced by more than just Transparency mode
Godot **automatically** switches a material to alpha blending (with all its limitations: no shadows, sorting issues, slower) if ANY of these are enabled:
- Transparency mode set to **Alpha**
- Blend mode set to anything other than **Mix** (Add, Sub, Mul, etc.)
- **Refraction** enabled
- **Proximity Fade** enabled
- **Distance Fade** set to **Pixel Alpha** mode
If you accidentally enable one of these properties, your material loses shadow casting ability silently.


<!-- keywords: export, property, @tool -->
### @export setters/getters only run in editor if @tool is present
When editing exported properties in the Inspector, setters/getters on those properties are only executed if the script has `@tool` at the top. Without `@tool`, Inspector edits write the raw value directly, bypassing your setter logic. This means validation and visual updates you put in setters won't run at edit time — only at runtime.


<!-- keywords: WorldEnvironment, EDITOR, ONLY, Node, DirectionalLight3D, light, Environment -->
### WorldEnvironment preview sun/sky is EDITOR-ONLY — add nodes for game lighting
The default preview sun and sky you see in the 3D editor viewport are **NOT** visible in the running game. They exist only to help you see your scene during editing. To have sky and sun in the actual game, you must add:
- `WorldEnvironment` node with an `Environment` resource (for sky, fog, GI, post-processing)
- `DirectionalLight3D` node (for sunlight)
Only ONE `WorldEnvironment` node should exist per scene tree — adding more triggers a warning and unpredictable behavior. The background sky also affects ambient light and reflections, even if it's not directly visible on screen.


<!-- keywords: Texture, Image, Texture2D, texture -->
### Texture pixels not CPU-accessible directly — convert to Image first
A `Texture2D` stored in GPU video memory cannot have its pixel data read directly from GDScript. To read or modify pixels, call `texture.get_image()` to download to an `Image` (slow — avoid per-frame). Then use `image.get_pixel(x, y)` / `image.set_pixel(x, y, color)`. To upload modified pixels back: create a new `ImageTexture.create_from_image(image)`.


<!-- keywords: XR, AR -->
### PNG is 8-bit only — use EXR or HDR for high-dynamic-range images
PNG format is limited to 8 bits per channel in Godot (even if the source file is 16-bit). For HDR panorama skies or lightmaps, use `.exr` or `.hdr` format. JPEG does not support transparency. SVG: text must be converted to paths in the vector editor before importing, and complex SVGs may not render correctly.


<!-- keywords: export -->
### @export_range: 'or_greater'/'or_less' allow values outside the slider range
By default, `@export_range(min, max)` clamps Inspector input to the range. Add `"or_greater"` and/or `"or_less"` hints to allow typing or dragging values outside the range:
  @export_range(0, 100, 1, "or_greater") var speed: int  # Can exceed 100
For angle variables stored in radians but shown as degrees in the Inspector, use:
  @export_range(-180, 180, 0.1, "radians_as_degrees") var angle: float


<!-- keywords: Resource, instantiate, load, preload -->
### load() and preload() return a cached, shared Resource — use .duplicate() for separate instances
Godot caches every resource loaded from disk. Calling `load("res://item.tres")` twice returns the **same object**. If multiple enemies should each have their own independent `stats` resource (so modifying one doesn't affect others), call `.duplicate()` after loading:
  var stats = load("res://base_stats.tres").duplicate()  # independent copy
For deep nesting (sub-resources inside resources), pass `true` to `duplicate(true)`. `preload()` has the same caching behavior.


<!-- keywords: COMPILE, TIME, String -->
### preload() is COMPILE-TIME only — path must be a constant string literal
`preload()` loads a resource when the script is compiled, not at runtime. Therefore its argument must be a **literal string constant**, not a variable:
  var r = preload("res://my_scene.tscn")  # OK
  var path = "res://my_scene.tscn"
  var r2 = preload(path)  # ERROR: path is a variable
Use `load(path)` or `ResourceLoader.load(path)` when the path is dynamic. `preload()` is unavailable in C#.


<!-- keywords: Resource, CANNOT, export, property, inner class -->
### Resource inner class CANNOT serialize @export properties
Defining a Resource subclass as an **inner class** of another script (using the `class` keyword inside a script) will NOT properly serialize `@export` properties when saved with `ResourceSaver.save()`:
  # WRONG — inner class
  extends Node
  class MyRes extends Resource:
      @export var value = 5  # NOT saved!
  # CORRECT — use a separate script file with class_name
Always place custom Resource classes in **their own .gd file** with `class_name` at the top. This is required for proper Inspector editing and serialization.


<!-- keywords: Node, _ready, scene, _process -->
### Node lifecycle order: _ready() is post-order, _process() is pre-order
In Godot's scene tree, callback order differs by function:
- `_process()`, `_physics_process()`, `_input()`, `_enter_tree()`: **pre-order** (parent called BEFORE children — top-to-bottom in the editor)
- `_ready()`: **post-order** (children called BEFORE parent — children are fully ready when parent's _ready() runs)
- `_exit_tree()`: **reverse pre-order** (bottom-to-top)
So in `_ready()` you can safely call methods on child nodes. In `_enter_tree()` the children may not be ready yet.


<!-- keywords: Autoloads, MUST, queue_free, Node, autoload -->
### Autoloads MUST NOT be freed — engine crash if you call free() or queue_free() on them
Autoloaded nodes are managed by the engine and persist for the entire game session. Calling `free()` or `queue_free()` on an autoload node **crashes the engine**. To 'reset' an autoload, expose a reset method instead. Autoloads are NOT freed when changing scenes with `change_scene_to_file()`.


<!-- keywords: Autoload, scene, Node, get_node, autoload, singleton -->
### Autoload is NOT a true singleton — it's just a node added before the game scene
When you autoload a script, Godot creates a Node, attaches the script, and adds it as a direct child of the root Viewport **before** any game scenes load. It can still be instantiated manually. You can access it two ways:
  MyAutoload.some_method()   # if 'Enable' is checked (GDScript only)
  get_node("/root/MyAutoload")  # always works
In an autoload's `_ready()`, the game's main scene is the **last** child of root: `get_tree().root.get_child(-1)`.


<!-- keywords: Freeing, signal, scene -->
### Freeing a scene from within its own running signal callback crashes — use call_deferred()
If a signal fires (e.g., a button pressed) and the callback calls `current_scene.free()` or `get_tree().change_scene_to_file(...)`, the node may still be executing code, causing a crash or undefined behavior. **Always defer scene switches**:
  func _on_button_pressed():
      # WRONG: get_tree().change_scene_to_file("res://next.tscn")
      change_level.call_deferred("res://next.tscn")  # safe


<!-- keywords: Compatibility, Shader, shader, rendering -->
### Compatibility renderer: manually warm up shaders to avoid first-frame stutter
The Compatibility (OpenGL) renderer cannot use the ubershader approach used by Forward+/Mobile. Shaders compile the first time a mesh/material appears on screen, causing visible stutter. Workaround: **spawn all unique meshes and particle effects off-screen for one frame** at level load (behind a fullscreen ColorRect). Forward+ and Mobile handle this automatically since Godot 4.4 via ubershaders.


<!-- keywords: Windows, Exclusive, Fullscreen -->
### Windows: use Exclusive Fullscreen, not Fullscreen, for games
Godot has two 'fullscreen' window modes:
- `WINDOW_MODE_FULLSCREEN`: leaves a 1-pixel-high gap at the bottom so Windows treats it as an overlay window. Intended for GUI apps needing per-pixel transparency. Windows does NOT give it elevated priority — more jitter and input lag.
- `WINDOW_MODE_EXCLUSIVE_FULLSCREEN`: uses actual screen dimensions. Windows gives it elevated GPU scheduling priority, reducing jitter and input lag. **Use this for games.**
Also note: when using integer scaling for pixel art, the 1-pixel gap in regular Fullscreen can cause the scale factor to be 1 smaller than expected.


<!-- keywords: Pixel, Viewport, rendering, int -->
### Pixel art games: use viewport stretch mode + integer scale mode
For pixel art, set:
  Project Settings > Display > Window > Stretch:
    Mode = viewport       # render at base resolution, then scale
    Aspect = keep (or expand)
    Scale Mode = integer  # CRITICAL: prevents uneven pixel scaling
Without integer scale mode, a 640x360 viewport on a 1366x768 screen scales at 2.133x, making pixels appear inconsistently sized. With integer mode, it scales to exactly 2x (1280x720) with black bars. A good pixel art base: 640x360 (scales cleanly to 720p, 1080p, 1440p, 4K).


<!-- keywords: Camera, Keep, Height, Hor, FoV, Width, Vert, Camera3D, 3D -->
### 3D Camera FOV: default Keep Height (Hor+) widens FoV for widescreen; use Keep Width (Vert-) for portrait mobile
Camera3D.keep_aspect defaults to KEEP_HEIGHT: wider screens automatically see more horizontally (Hor+). This is correct for desktop and landscape mobile. For **portrait mobile games**, switch to KEEP_WIDTH (Vert-): taller screens see more vertically instead of getting a cropped view. Set via Inspector: Camera3D > Keep Aspect > Keep Width.


<!-- keywords: SubViewport, SubViewportContainer, Viewport, Container, Input, input -->
### SubViewport: input not received by default, must be inside SubViewportContainer
A `SubViewport` node does NOT automatically receive mouse/touch/keyboard input unless it is a direct child of a `SubViewportContainer`. Without the container, forward input manually. Additionally:
- For 3D/2D audio positional sound to work inside a SubViewport, enable `audio_listener_enable_3d` / `audio_listener_enable_2d` on the viewport.
- Shadows in SubViewports require `positional_shadow_atlas_size > 0` (default 4096 on desktop, 2048 on mobile).
- SubViewports use their own separate World2D (always). World3D is shared with parent by default unless `own_world_3d=true` is set.


<!-- keywords: Viewport, EMPTY, RenderingServer, await, Texture, texture, rendering -->
### Viewport texture capture is EMPTY on first frame — await RenderingServer.frame_post_draw
Calling `get_viewport().get_texture().get_image()` in `_ready()` or at the start of the first frame returns an empty image because rendering hasn't happened yet. To capture a screenshot or viewport contents, wait for the frame to finish:
  await RenderingServer.frame_post_draw
  var img = get_viewport().get_texture().get_image()


<!-- keywords: GDScript, gdscript -->
### GDScript 'as' keyword returns null silently on type mismatch — does NOT throw
Unlike C++ or C# casting, GDScript's `as` operator never throws an exception. If the object is not of the target type, it silently returns `null`. This can cause subtle bugs:
  var player := body as Player  # null if body is not a Player — no error!
  player.take_damage(10)  # CRASH: null reference, no prior warning
Always null-check after `as`, or use `is` first. Prefer `is` for checking + direct cast for reliability:
  if body is Player:
      var player: Player = body


<!-- keywords: GDScript, Arrays, Dicts, gdscript, Array, typed -->
### GDScript typed Arrays/Dicts: nested types are NOT supported
Typed containers like `Array[int]` and `Dictionary[String, int]` are supported, but **nested typed collections are syntax errors**:
  var scores: Array[int] = []          # OK
  var nested: Array[Array[int]] = []   # SYNTAX ERROR
  var map: Dictionary[String, Dictionary[String, int]] = {}  # SYNTAX ERROR
Use `Array[Array]` (untyped inner) as a workaround. Element types must be built-in types, native classes, or custom classes — not other typed collections.


<!-- keywords: Renderer, Forward, Compatibility, rendering -->
### Renderer capabilities: know what requires Forward+ vs Compatibility
Key features ONLY available in specific renderers:
- Forward+ only: VoxelGI, SDFGI, SSR (screen-space reflections), SSIL, TAA, FSR2, PCSS for DirectionalLight, normal/roughness buffer, sub-surface scattering, volumetric fog
- Forward+ and Mobile only: Decals, particle trails, depth-of-field blur, MSAA 2D, FXAA, SMAA, light projector textures, compositor effects
- Compatibility only renderer for: Web platform (no other option), very old hardware without Vulkan/D3D12/Metal
- Compatibility shader limitation: no ubershader → must manually warm up shaders (see above)
When writing code that uses advanced effects, always check if the target renderer supports them. The renderer is set per-project but can be overridden at runtime on some platforms.


<!-- keywords: Physics, physics, Input, input, int -->
### Physics interpolation reduces jitter but increases input lag — careful for fighting/rhythm games
Enabling physics interpolation (Project Settings > Physics > Common > Enable Object Motion Interpolation) smooths jitter caused by physics/framerate mismatch. However, it **adds one physics tick of input lag** to physics-driven movement. For most games this is acceptable. For competitive games requiring tight input response (fighting games, rhythm games), either disable it or compensate by increasing the physics tick rate (e.g., to 120 Hz). Also: when teleporting objects with interpolation enabled, call `Node3D.reset_physics_interpolation()` to prevent visible interpolation from old to new position.


<!-- keywords: Scripts, RefCounted, Node -->
### Scripts without 'extends' implicitly extend RefCounted — NOT Node
A GDScript file that has no `extends` keyword at all implicitly extends `RefCounted`. This means you cannot attach such a script to a Node in the scene tree. If you get an error like 'Script does not inherit from Node', check that your script has `extends Node` (or the appropriate class) at the top. The implicit `extends RefCounted` is useful for pure data/logic classes, but is often an oversight when the intent was to create a Node script.


<!-- keywords: Object, RefCounted, instantiate -->
### Object (non-RefCounted) references go stale silently — use is_instance_valid()
`Object` (not `RefCounted`) instances are NOT reference-counted. When a `Node` is freed (via `free()` or `queue_free()`), any variables still holding a reference become a **dangling pointer** in GDScript. Accessing a freed object crashes with 'Invalid instance'. Always guard with `is_instance_valid(obj)` before using stored Object/Node references:
  if is_instance_valid(enemy):
      enemy.take_damage(10)
Alternatively, use `WeakRef` to hold a weak reference: `var ref = weakref(enemy)` and `ref.get_ref()` returns null after the object is freed. Note: `RefCounted` subclasses (including `Resource`) are reference-counted and are automatically freed when there are no more references — no need for `is_instance_valid()` there.


<!-- keywords: INVISIBLE, Inspector, export -->
### @export_storage: serialized to file but INVISIBLE in the Inspector
`@export_storage` marks a variable to be serialized when saving a scene/resource, but does NOT show it in the Inspector. Use it for internal state that should persist across saves (e.g., a character's current HP that is managed by code) but shouldn't be accidentally changed in the Inspector. This is distinct from:
- `@export`: shown in Inspector AND serialized
- No annotation: shown in Inspector if it's a simple type, but NOT serialized in .tscn/.tres files
- `@export_storage`: NOT shown in Inspector, but IS serialized


<!-- keywords: Reading, DEFAULT, Inspector, export -->
### Reading @export var in _init() gets DEFAULT value, not the Inspector-set value
When a script is instantiated, `_init()` is called BEFORE the scene file is applied (i.e., before Inspector-set export values are loaded). If you read an `@export var` in `_init()`, you get its **default value** as declared in the script, not whatever was typed in the Inspector:
  @export var speed = 100.0
  func _init():
      print(speed)  # ALWAYS prints 100.0, even if Inspector shows 200.0
To use Inspector-set values, read them in `_ready()` or later. This also applies to `_enter_tree()` — export values are not yet applied there.


<!-- keywords: SceneTree, scene -->
### NOTIFICATION_PARENTED fires even when NOT in the SceneTree
`NOTIFICATION_PARENTED` (and its counterpart `NOTIFICATION_UNPARENTED`) is emitted whenever a node is **added as a child** of another node, regardless of whether either node is in the active SceneTree. This is useful for nodes that need to react to being connected in a hierarchy even during deferred loading. In contrast, `NOTIFICATION_ENTER_TREE` fires only when the node and its ancestors are added to the running SceneTree. Don't confuse the two.


<!-- keywords: Set, BEFORE, Node, add_child, scene -->
### Set node properties BEFORE add_child() for correct initialization order
When creating and configuring nodes at runtime, set ALL properties before calling `add_child()`. Once a node enters the tree, `_ready()` fires on it (and its children), which may trigger signal connections or layout recalculations. Setting properties afterward can cause a double-initialization. Canonical pattern:
  var enemy = EnemyScene.instantiate()
  enemy.health = 200      # set BEFORE adding to tree
  enemy.position = spawn_point
  add_child(enemy)        # _ready() fires here with correct values


<!-- keywords: int, print, debug -->
### print() output may NOT appear in release builds until game exits — use printerr() for realtime logging
`print()` in Godot writes to **stdout** with buffering. In release builds (and in some launchers), stdout is not flushed until the process exits, meaning `print()` messages may never appear in real time. By contrast, `printerr()` writes to **stderr** which is **unbuffered** (always flushed immediately). For debug logging that you need to see in real time (especially if the game might crash), use `printerr()`. You can also call `print_rich()` which flushes immediately in the editor. In editor/debug builds, `print()` does flush immediately.


<!-- keywords: Custom, Mutex, Thread, threading -->
### Custom logger _log_message() is called from multiple threads — protect with Mutex
In Godot 4.5+, you can override `_log_message()` in an autoload to intercept all `print()`, `push_error()`, `push_warning()` calls. However, **_log_message() can be called from any thread** (audio, physics, background threads). If your logger writes to a file, array, or any shared state, protect it with a Mutex:
  var _mutex = Mutex.new()
  func _log_message(msg: String, is_error: bool) -> void:
      _mutex.lock()
      log_file.store_line(msg)
      _mutex.unlock()
Without a Mutex, two threads writing simultaneously can corrupt the log data or crash.


<!-- keywords: Projectiles -->
### Projectiles must NOT be children of the shooter — they inherit transform and get deleted with parent
A common mistake: making a bullet/projectile a child of the player or enemy that fires it. Problems:
1. The projectile **inherits the parent's transform** — moving the parent moves the bullet too.
2. If the parent is freed (enemy dies), all its children including mid-flight projectiles are freed.
Instead, emit a signal that a root-level manager node handles, or `add_child()` the projectile to the **scene root** or a dedicated 'projectiles' node:
  # In player.gd
  signal bullet_fired(bullet_scene, global_pos, direction)
  # In main.gd or a BulletManager autoload
  func _on_player_bullet_fired(scene, pos, dir):
      var b = scene.instantiate()
      get_tree().root.add_child(b)  # root-level, not under player
      b.global_position = pos


<!-- keywords: Scene, Name, scene, Node, get_node, Unique, Nodes, SAME, NodeName, instantiate -->
### Scene unique nodes (%Name) only work within the same scene — not across scene boundaries
The `%NodeName` syntax (Unique Name access) resolves to a node tagged with 'Access as Unique Name' in the editor. It only searches **within the same scene file** that the script belongs to. If you try `%SomeNode` from a script in scene A, but `SomeNode` is in an instantiated child scene B, it will NOT be found — even if `SomeNode` exists and is visible in the scene tree. You must either:
- Tag it as unique within scene B and access it from a script also in scene B
- Use `$ChildSceneRoot/SomeNode` or `get_node()` with the full relative path from scene A
Also: `find_child("SomeNode", true)` recursively scans ALL descendants — it is significantly slower than `%Name` (which is cached) or direct `$Path`. Avoid `find_child()` in `_process()` loops.


<!-- keywords: AnimationTree, RESET, AnimationPlayer, animation -->
### AnimationTree: RESET animation is required for correct blending
When blending two animations in AnimationTree and one animation lacks a property track that the other has, the missing animation uses an **initial value** for that property. For most properties, this initial value is `0` (or `Vector2(0,0)`) — NOT the current node value. This causes jarring jumps. The fix: create a `RESET` animation in AnimationPlayer (exact name, case-sensitive) and add tracks for every property you want to blend, with a single keyframe at time 0 set to the correct default. AnimationTree uses RESET as the reference for blend calculations. For Skeleton3D bones, the initial value is Bone Rest instead of 0.


<!-- keywords: AnimationTree, animation, Bone, import -->
### AnimationTree: humanoid models should be imported in T-pose for correct bone blending
Linear Angle and Cubic Angle rotation track modes interpolate along the SHORTEST path from the initial pose (Bone Rest / RESET animation). For humanoid characters, if bones are in T-pose rest, the shortest rotation path naturally avoids bones passing through the body. If the rest pose is not T-pose, the shortest rotation path may cause bones to penetrate through the mesh during blends. Import humanoid 3D characters in T-pose to set correct Bone Rest values.


<!-- keywords: AnimationTree, Advance, Condition, Expression, animation -->
### AnimationTree Advance Condition cannot check 'false' — use Advance Expression
StateMachine Advance Condition only checks if a named parameter is `true`. It does NOT support negation (`!`) or any expression. To have a transition trigger when a condition is false, you need two separate boolean variables (`is_walking` and `is_not_walking`) set to opposite values. A better alternative: use **Advance Expression** (available in Godot 4) which supports full boolean expressions like `!is_walking`, `speed > 0`, or `player.is_on_floor()`. When using Advance Expression with script variables, set the **Advance Expression Base Node** in the AnimationTree inspector to point to the node that contains the script with those variables.


<!-- keywords: Feature -->
### Feature tags are immutable — OS.has_feature('mobile') is false on web/mobile
Feature tags like `pc`, `mobile`, `web`, `android` are set at compile time based on the **export platform**, not at runtime based on the device. If you export to Web and the player runs on an Android phone, `OS.has_feature('mobile')` returns **false** (it's a web export, not Android). To detect web-on-mobile, use `OS.has_feature('web_android')` or `OS.has_feature('web_ios')`. The only dynamic feature tags are texture compression, `web_<platform>`, and `movie`.


<!-- keywords: ProjectSettings, settings -->
### ProjectSettings.get_setting() ignores feature tag overrides — use get_setting_with_override()
When you define per-feature-tag overrides in Project Settings (e.g., different screen resolution for `mobile`), they are stored as e.g. `section/key.mobile`. Using `ProjectSettings.get_setting("section/key")` always returns the base value, ignoring feature tag overrides. To get the override-aware value based on the current feature tags, use `ProjectSettings.get_setting_with_override("section/key")`. Custom feature tags defined in export presets are also only available in exported builds, not when running from the editor.


<!-- keywords: Collision, Scale, NEVER, collision, physics -->
### Collision shape Scale must stay (1,1,1) — NEVER scale via transform handles
Scaling a CollisionShape2D/3D using the node's scale property or the transform scale handles causes **broken collision behavior** (incorrect collision bounds, tunneling, etc.). To resize a collision shape, use the **shape's own size handles** in the editor (the orange dots) or set the `shape.size` / `shape.radius` / `shape.height` properties directly. The Transform > Scale in the Inspector should always remain at (1,1) for 2D or (1,1,1) for 3D. This is one of the most common physics bugs.


<!-- keywords: Collision, LAYER, MASK, collision, physics -->
### Collision LAYER = what object you are; Collision MASK = what objects you can collide with
For two objects to collide, **BOTH conditions must be met**:
- Object A's collision **mask** must include the layer that Object B is **on**
- Object B's collision **mask** must include the layer that Object A is **on** (for Area2D, only one direction needs to match)
A common mistake: set both layer AND mask to the same layer and wonder why an enemy doesn't detect a coin. The enemy's mask must include the coin's layer. Name your layers in Project Settings > Layer Names > 2D/3D Physics. Set them per-bit using `set_collision_layer_value(layer_num, bool)` in code (layer numbers start at 1, not 0).


<!-- keywords: RigidBody, NEVER, physics, int, global, RigidBody3D, GDScript -->
### RigidBody: NEVER call set_global_transform()/look_at() every frame — use _integrate_forces()
Calling `look_at()`, `set_global_transform()`, or directly setting `position`/`rotation` on a RigidBody every frame overrides the physics engine's state and **breaks simulation** (the body appears to teleport, no collision response, etc.). For one-time placement, these are fine. For continuous control, use the `_integrate_forces(state)` callback and modify `state.angular_velocity` or `state.linear_velocity` instead. To make a RigidBody look at a target, calculate the angular velocity needed to rotate toward the target within `_integrate_forces()` (see Godot docs for the `look_follow()` pattern).


<!-- keywords: Thread, MANDATORY, Windows, threading -->
### Thread: wait_to_finish() is MANDATORY — also, creating threads is slow on Windows
After a `Thread.start()`, you **must** call `thread.wait_to_finish()` when done — even if you know the thread function has already returned. Skipping it causes memory leaks or crashes. Always call it in `_exit_tree()` or equivalent cleanup. Additionally, thread creation is expensive on Windows (much more so than on Linux/macOS). Avoid creating threads just-in-time for short tasks. Instead, pre-create threads during level loading and keep them alive for the session, using Semaphores to signal work.


<!-- keywords: Scene, scene, Tree, Thread, threading -->
### Scene tree is NOT thread-safe — use call_deferred() from worker threads
The active scene tree is **not thread-safe**. You cannot call `add_child()`, `remove_child()`, `queue_free()`, or set most node properties from a background thread. Doing so can cause random crashes or corruption. Use `call_deferred()` or `set_deferred()` to queue the operation for the main thread:
  # In a worker thread:
  node.add_child.call_deferred(child)  # safe
  # NOT safe: node.add_child(child)
Exception: you CAN instantiate and configure nodes **outside** the scene tree in a background thread, then add them to the tree via call_deferred from the main thread. The NavigationServer is thread-safe. AStar2D/AStar3D are NOT thread-safe (one AStar per thread only).


<!-- keywords: GDScript, Array, Dictionary, RESIZING, gdscript, Thread, Mutex, threading -->
### GDScript Array/Dictionary: element access is thread-safe; RESIZING is NOT
Reading and writing individual elements of an Array or Dictionary from multiple threads simultaneously is safe in GDScript. However, **any operation that changes the size** (append, push_back, erase, resize, insert, remove_at, clear) requires a Mutex, as it modifies the container's internal structure and can corrupt it:
  # SAFE without mutex (reading/writing existing element):
  array[index] = value
  # UNSAFE without mutex (size change):
  array.append(value)   # must lock mutex first


<!-- keywords: BackBufferCopy, Node, Texture, texture, Shader, shader, int, 2D -->
### hint_screen_texture in 2D: overlapping nodes with this shader don't see each other — use BackBufferCopy
In 2D, when Godot sees a node using `hint_screen_texture`, it copies the entire screen to a back buffer exactly ONCE before drawing that node. Later nodes using the same hint don't get a new copy — they see the same original screen (without earlier hint_screen_texture nodes applied). This means two overlapping screen-reading shader objects won't see each other's effects. Fix: place a `BackBufferCopy` node between them in the scene tree to force an additional screen copy. In 3D, the screen texture is captured once after opaque geometry but BEFORE transparent geometry — transparent objects won't appear in the screen texture.


<!-- keywords: Shader, shader, light -->
### Shader light() function disabled on mobile by default — vertex lighting
The `light()` processor function in spatial shaders will NOT run if:
- The shader has `render_mode vertex_lighting` set
- **Project Settings > Rendering > Quality > Shading > Force Vertex Shading** is enabled (which is the DEFAULT on mobile platforms)
If your shader uses a custom `light()` function for effects like rim lighting or custom specularity, these will silently disappear on mobile. Verify this setting for mobile exports. On desktop, this setting is off by default.


<!-- keywords: Call, Method, Track, AnimationPlayer, animation -->
### AnimationPlayer: Call Method Track events NOT executed in editor preview
When previewing an animation in the AnimationPlayer editor panel, **Call Method Track keyframes are NOT executed** (this is intentional, for safety — you don't want `queue_free()` to delete nodes while editing). This means you cannot test Call Method Track timing in the editor by scrubbing. You must run the game (F5) to test method calls. Similarly, audio tracks only play audio in-editor if using the Animation panel's play button (not when scrubbing the timeline).


<!-- keywords: AnimationPlayer, RESET, animation, AnimationTree -->
### AnimationPlayer: RESET animation name is case-sensitive and must be exactly 'RESET'
The special 'default pose' animation must be named exactly `RESET` — all caps, no spaces. It is used by:
1. `Reset On Save`: saves the scene with the reset pose applied (property on AnimationPlayer)
2. AnimationTree blending: provides the reference values for properties not present in all animations
3. `Apply Reset` in the Animation panel's Edit menu to restore the default pose in the editor
RESET should contain exactly ONE keyframe at time 0 for each property you want to reset. It is not meant to be played back as a regular animation.


<!-- keywords: Web, AudioStreamPlayer, audio, export -->
### Web export: C# NOT supported; audio effects NOT supported; fullscreen needs user gesture
Critical web export limitations:
- **C# projects cannot be exported to Web** in Godot 4 (use GDScript or C++/GDExtension)
- **AudioEffects are not supported** in the default 'Sample' playback mode (Web Audio API). Switch AudioStreamPlayer Playback Type to 'Stream' for effects, but this increases latency
- **Entering fullscreen** or capturing the mouse cursor requires a direct response to a user input event (click, keypress) — cannot be done from _ready() or a timer. The fullscreen project setting doesn't work on Web without a custom HTML shell
- **Tab inactive** = all _process() stops. Networked games may disconnect
- **Gamepads** are not detected until the player presses a button
- **Low-level UDP/TCP networking not available** — only HTTP, WebSocket, WebRTC
- **Multi-threading** requires CORS headers on the server; single-threaded export is the new default/preferred approach since 4.3


<!-- keywords: Input, InputEvent, input -->
### Input event flow order: _input() → GUI → _shortcut_input() → _unhandled_key_input() → _unhandled_input()
Godot dispatches InputEvents through nodes in a strict order (deepest-child-first, reverse depth-first):
1. `_input()` — called first on all nodes, even while GUI is focused. Use for raw input.
2. GUI Controls get the event via `_gui_input()`. If a Control `accept_event()`, propagation stops.
3. `_shortcut_input()` — for keyboard shortcuts and joypad buttons only.
4. `_unhandled_key_input()` — for unhandled keyboard events only.
5. `_unhandled_input()` — for everything else. **Best for gameplay input**: only fires if GUI didn't consume the event.
Important: SubViewports do NOT receive input unless inside a SubViewportContainer. GUI keyboard/joypad events do NOT travel up the scene tree (unlike mouse events).


<!-- keywords: AVOID, Transform3D, Basis, Quaternion, property, Node3D, 3D, rotation -->
### 3D rotations: AVOID using the .rotation property — use Transform3D/Basis/Quaternion
Euler angles (.rotation on Node3D) have two fundamental problems in 3D:
1. **Axis order dependency**: The final rotation depends on which axis is rotated first. Rotating Y then X gives a different result than X then Y (FPS camera needs Y-first for correct behavior).
2. **Gimbal lock**: When two axes align, a degree of freedom is lost.
3. **Non-linear interpolation**: Lerping Euler angles can rotate the wrong direction (the 270°→0° vs 270°→360° problem).
Use `transform.basis` (or Quaternion / Transform3D) for rotations in game code. Godot's 3D convention: X=Right, Y=Up, Z=Back (toward camera).


<!-- keywords: Physics, NEVER, CollisionShapes, Resources, SHARED, collision, physics, Resource, CollisionShape -->
### Physics: NEVER scale CollisionShapes, and collision shape Resources are SHARED
Two critical physics gotchas:
1. **Scaling physics bodies doesn't work**: Godot doesn't support non-uniform scaling of physics bodies or shapes. Instead, change the shape's `extents`, `radius`, or `height` directly. Change the visual node's scale separately. The CollisionShape must NOT be a child of the scaled visual node in this case.
2. **Collision shape Resources are shared**: When you change a collision shape's size at runtime, it affects ALL nodes using the same resource. Call `.duplicate()` on the resource first:
  collision_shape.shape = collision_shape.shape.duplicate()
  collision_shape.shape.extents = new_size


<!-- keywords: Physics, Continuous, physics -->
### Physics tunneling: use Continuous CD or thicker colliders for fast-moving objects
At high speeds, objects can pass through thin colliders between physics frames (tunneling). Solutions:
- Enable **Continuous CD** in RigidBody properties
- Make static colliders thicker than their visual representation
- Increase Physics Ticks per Second (default 60 Hz; use multiples: 120, 180, 240)
Also: TileMap edges cause bumps when objects slide across them. Fix: use composite colliders (one shape per connected tile group). In Godot 4.5+, TileMapLayer does this automatically via **Physics Quadrant Size**.


<!-- keywords: Physics, Cylinder, GodotPhysics, Box, Capsule, physics -->
### Physics: Cylinder shapes are unstable in GodotPhysics — use Box or Capsule
CylinderShape3D is known to be unreliable in Godot's built-in GodotPhysics engine (remnant of the Bullet→GodotPhysics migration). For character controllers, use BoxShape3D (most reliable) or CapsuleShape3D (no diagonal enlargement). Alternatively, switch to the **Jolt** physics engine (4th option in Project Settings > Physics > 3D > Physics Engine) for better stability and cylinder support.


<!-- keywords: Physics, physics, int, float -->
### Physics: floating-point precision degrades far from the world origin
Physics simulation (and rendering) loses precision as coordinates grow large. Objects far from (0,0,0) exhibit wobbling, jitter, and incorrect collisions. For large open worlds, enable **Large World Coordinates** (double-precision mode) in Project Settings, or implement an origin-shifting system that moves the world around the player to keep the player near (0,0,0).


<!-- keywords: Raycasting, Area3D, physics, Area2D, _process, _physics_process, RayCast -->
### Raycasting: must be done in _physics_process(); Area3D not hit by default
Two non-obvious raycast rules:
1. **Space is locked outside _physics_process()**. Calling `get_world_3d().direct_space_state` from `_process()` or `_ready()` causes a 'space locked' error. Always do ray queries inside `_physics_process()` (or `_physics_process()` via call_deferred).
2. **Area3D/Area2D are not hit by default**: Must set `query.collide_with_areas = true` on the PhysicsRayQueryParameters object.
3. **Raycast from a CollisionObject hits itself**: Add `query.exclude = [self]` (or `[get_rid()]` in C#) to exclude the caster.
4. **Raycast result is an empty Dictionary on miss** (not null): Check `if result:` not `if result != null:`.


<!-- keywords: Node, rendering, 3D -->
### 3D rendering: transparent objects sort by node position, not vertex — use render_priority
Transparent materials in Godot are sorted back-to-front by the **node's origin position**, not by individual vertex distance. This causes incorrect sorting when:
- Two transparent objects overlap
- A single large transparent mesh has parts at different distances
Fix with `material.render_priority` (higher = rendered in front of other transparents) or `VisualInstance3D.sorting_offset`. Better alternatives to alpha blending:
- **Alpha Scissor**: for textures with hard transparent/opaque edges (no sorting, faster, slight aliasing)
- **Depth Pre-Pass**: renders the opaque silhouette first, then blends
- **Pixel/Object Dither**: for distance fade effects (no actual transparency blending)
Note: transparent objects don't appear in the normal-roughness buffer, so screen-space effects like SSR won't affect them.


<!-- keywords: INCREASING, Camera, Near, Far, rendering, Camera3D, 3D -->
### 3D rendering: Z-fighting — fix by INCREASING Camera Near, not decreasing Far
Z-fighting (flickering textures) occurs when two objects map to the same depth buffer value. The depth buffer is 24-bit on desktop, sometimes 16-bit on mobile. The precision is NON-LINEAR — it's much denser near the camera and sparse far away. To fix Z-fighting:
- **Increase Camera3D.near** (minimum clip distance): moves depth precision to start further out, giving more precision in the distance. This has the greatest impact.
- Decrease Camera3D.far only slightly helps compared to near.
- On mobile (16-bit depth), Z-fighting is much more common. Keep near as high as possible.


<!-- keywords: Texture, texture, rendering, 3D, Mobile -->
### 3D rendering: mobile textures max 4096x4096; use power-of-two for repeating textures
Mobile GPU texture constraints:
- Maximum texture size: typically 4096×4096 (some mobile: 2048×2048)
- **Non-power-of-two textures may not repeat** on some mobile GPUs. If a texture needs to tile, keep its dimensions at powers of 2 (64, 128, 256, 512, 1024, 2048, 4096)
- **VRAM texture compression does NOT work for transparent textures on Android** (only opaque). Transparent textures on Android may use more memory than expected
- **VRAM compression should be DISABLED for pixel art** — the compression artifacts are very visible at low resolution without any performance benefit


<!-- keywords: Forward, Mobile, Texture, texture, rendering, int, 3D -->
### 3D rendering: color banding — enable debanding for Forward+/Mobile, or bake noise into textures
Color banding (visible 'steps' in gradients, especially in dark areas or large solid-color surfaces) occurs because Godot renders in HDR internally but outputs to a lower-precision buffer. Severity by renderer:
- **Forward+**: best precision, enable `use_debanding` in Project Settings > Rendering > Anti Aliasing to apply a cheap fullscreen post-process fix
- **Mobile**: lower precision than Forward+, also supports debanding post-process
- **Compatibility**: lowest precision (no HDR), affects both 3D and 2D smooth gradients. Debanding must be done via custom shader or baking noise into textures


<!-- keywords: Mobile, OmniLights, SpotLights, MESH, Mesh, mesh, rendering, light, 3D, RESOURCE, scene, Resource -->
### 3D lights: Mobile renderer hard limit of 8 OmniLights + 8 SpotLights PER MESH
Light limits vary by renderer:
- **Forward+**: Up to 512 combined OmniLights + SpotLights + Decals + ReflectionProbes in camera view (adjustable in Project Settings > Rendering > Limits > Cluster Builder). Essentially unlimited lights as long as performance allows.
- **Mobile**: Hard limit of **8 OmniLights + 8 SpotLights per mesh resource** (not per scene, per mesh). Also max 256 per view. These limits CANNOT be changed. Lights exceeding the limit pop in/out during camera movement.
- **Compatibility**: 8 OmniLights + 8 SpotLights per mesh (can be increased in Project Settings > Rendering > Limits > OpenGL at performance cost).
- All renderers: max **8 DirectionalLights** simultaneously. Each additional one with shadows reduces ALL shadow atlas resolution.
Fix: use baked lightmaps, reduce dynamic lights, or split meshes (also improves culling).


<!-- keywords: Light, Cull, Mask, light, Shadow, Caster, STILL, DirectionalLight3D, 3D -->
### 3D lights: Light Cull Mask ≠ Shadow Caster — excluded objects STILL cast shadows
A common confusion: the light's **Cull Mask** controls which objects RECEIVE illumination from the light. Objects NOT in the cull mask won't be lit, but they will **still cast shadows** onto lit objects. To also prevent an object from casting shadows, change the object's **GeometryInstance3D > Cast Shadow** property, or adjust the light's **Shadow Caster Mask** (if available). DirectionalLight3D also has a **Shadow Max Distance** — keep this as LOW as possible for the best shadow quality and performance (fewer objects need shadow maps). The DirectionalLight3D's **position in the scene doesn't matter** — only its rotation (direction) does.


<!-- keywords: Compatibility, light, 3D -->
### 3D lights (Compatibility): enabling shadows may drastically change light appearance
On the Compatibility renderer (OpenGL), enabling shadows on a light causes it to render in **sRGB color space** instead of linear space (due to mobile hardware limitations with multi-pass rendering). This can make the light appear significantly darker or different in color. To achieve a similar appearance to the unshadowed version, you may need to increase the light's **Energy** value after enabling shadows.


<!-- keywords: Multiplayer, RPCs, RPC -->
### Multiplayer @rpc: ALL RPCs must match between client and server — even unused ones
Godot validates RPC signatures using a checksum of ALL `@rpc`-annotated functions in a script. The checksum includes the annotation parameters, function name, return type, AND the node's path. If even ONE unused RPC is declared differently on client vs server, ALL RPCs in that script will fail — often with a cryptic unrelated error message. Rules:
1. Every `@rpc` function must be declared on BOTH client and server scripts
2. The annotation parameters must match exactly: `@rpc("any_peer", ...)` on both sides
3. Nodes using RPCs must have the same NodePath — use `add_child(node, true)` (`force_readable_name=true`) for dynamically spawned nodes
4. RPCs cannot serialize Objects or Callables — only Variant types


<!-- keywords: Multiplayer, RPC, network -->
### Multiplayer @rpc defaults: 'authority' and 'call_remote' — understand the difference
`@rpc` with no arguments is equivalent to `@rpc('authority', 'call_remote', 'reliable')`:
- **'authority'**: Only the multiplayer authority (server by default, unless changed with `set_multiplayer_authority()`) can call this RPC on remote peers. Clients calling it are ignored.
- **'any_peer'**: Any client can call this RPC on remote peers. Essential for client→server RPCs (player input, actions).
- **'call_remote'**: The function is NOT called on the sender. Only runs on recipients.
- **'call_local'**: The function ALSO runs on the local peer. Needed when the server is also a player (listen server).
- Server's peer ID is always **1**. Use `multiplayer.get_remote_sender_id()` inside an RPC to identify who called it.
- Godot's high-level networking uses **UDP only** — forward the UDP port (not TCP) when hosting.


<!-- keywords: Localization, Labels, Buttons, Label, RichTextLabel, UI, Button -->
### Localization: Labels/Buttons auto-translate text matching translation keys — can be a bug
Controls like Label, Button, and RichTextLabel automatically call `tr()` on their text. If the text matches a translation key in the loaded translations, it will be replaced with the translated string. This is usually desired for UI text but can accidentally translate player names, usernames, item names, etc. if they happen to match a key. Fix: set `Auto Translate > Mode = Disabled` in the Inspector on nodes displaying user-generated content.


<!-- keywords: Localization, Default, Latin, Cyrillic -->
### Localization: Default project font only supports Latin-1 — CJK/Cyrillic will show blank
Godot's built-in default font (the editor font) only covers the Latin-1 character set. If you're localizing to Russian, Chinese, Japanese, Korean, Arabic, etc., characters will appear as blank boxes (tofu) unless you:
1. Download a multilingual font (e.g., Noto Fonts from google.com/get/noto)
2. Import the TTF file as a FontFile resource
3. Set it as a custom font on your Control nodes (or globally in Theme)
Also: the resource remapping system for localized assets **doesn't work for DynamicFonts**. For font-per-language scenarios, use the DynamicFont fallback system instead.


<!-- keywords: Audio, RIGHT, LEFT, audio -->
### Audio buses route RIGHT to LEFT only — cannot route to buses on the right
Godot's audio bus layout enforces a LEFT-TO-RIGHT dependency order. Each bus can only send audio to a bus that is **further to the left** of it. The leftmost bus is always the **Master** bus, which sends to the audio output. You cannot route audio backwards (right to left) to prevent infinite loops. To avoid clipping, ensure the Master bus output never exceeds **0 dB** (that's the digital maximum — not the loudest possible, but the clip point). Typical working range is -60 dB to 0 dB; silence is around -60 to -80 dB.


<!-- keywords: StandardMaterial3D, Alpha, Material, material -->
### StandardMaterial3D: transparency mode 'Alpha' disables shadow casting and SSR
When a material's transparency is set to **Alpha** (alpha blending), the mesh:
- Cannot cast shadows (even if Cast Shadow is On)
- Is not visible in screen-space reflections (SSR)
- Is not written to the normal-roughness buffer
- Is automatically sorted back-to-front (by node origin position, not vertices) causing ordering issues
Use these alternatives for shadow-casting transparent meshes:
- **Alpha Scissor**: hard cut-off (foliage, fences) — CAN cast shadows, fastest
- **Alpha Hash**: dithered (realistic hair) — CAN cast shadows
- **Depth Pre-Pass**: opaque silhouette first, then blends — CAN cast shadows
Also note: Godot **forces Alpha blending** automatically if you enable Refraction, Proximity Fade, or Distance Fade — even if you didn't set the transparency mode explicitly.


<!-- keywords: Visibility, Fade, Self, Dependencies, rendering -->
### Visibility ranges (LOD): Fade mode 'Self' and 'Dependencies' force transparent rendering
GeometryInstance3D's visibility range (manual LOD) has a Fade Mode:
- **Disabled**: instant switch with hysteresis (no transparency cost) — best performance
- **Self**: alpha-blends the node out near its visibility boundary — forces transparent rendering during transition
- **Dependencies**: the parent node fades in its child nodes when it reaches its visibility limit — intended for HLOD setups
Visibility range distances are measured from the camera to the **AABB center** of the node, not any vertex. Use **Visibility Parent** for HLOD: child nodes automatically hide when the parent (distant LOD mesh) is visible. The visibility parent doesn't need to be a scene tree parent.


<!-- keywords: Scene, scene, import, resource -->
### OBJ files do NOT get automatic LOD generation — change import type to 'Scene'
By default, OBJ files are imported as individual mesh resources (not 3D scenes), so Godot's automatic mesh LOD generation is skipped. To enable LOD for OBJ files: select the OBJ in FileSystem dock → Import dock → change **Import As** to **Scene** → click **Reimport** (requires editor restart). GLTF, .blend, Collada, and FBX files automatically get LOD meshes generated on import.


<!-- keywords: MultiMeshInstance3D, SAME, instantiate, mesh -->
### MultiMeshInstance3D: ALL instances use the SAME LOD level simultaneously
LOD selection for MultiMeshInstance3D (and GPUParticles3D, CPUParticles3D) is based on the distance from the camera to the **closest point of the node's AABB**. All instances in the MultiMesh share that single LOD decision — you can't have some instances at LOD0 and others at LOD2. If instances are spread far apart, split them into separate MultiMeshInstance3D nodes for independent LOD. Also: GPUParticles3D needs **GPUParticles3D > Generate AABB** baked for correct LOD distance calculation.


<!-- keywords: Occlusion, MultiMesh, GPUParticles, Material, material, MeshInstance3D, mesh, CSG, GPUParticles3D, particles -->
### Occlusion culling baking excludes: MultiMesh, GPUParticles, CSG, and transparent materials
When baking occluders with OccluderInstance3D, only **MeshInstance3D nodes with opaque materials** are included in the bake. The following are NOT automatically baked as occluders:
- MultiMeshInstance3D, GPUParticles3D, CPUParticles3D
- CSG nodes (can be manually included after converting to MeshInstance3D in Godot 4.4+)
- Meshes with transparent materials (even if the texture is visually 100% opaque)
For these, manually add OccluderInstance3D with hand-placed primitive shapes. Also: the occludee's **entire AABB must be FULLY occluded** by the occluder shape to be culled — partial occlusion is insufficient. Moving OccluderInstance3D nodes at runtime is expensive (BVH rebuild); toggle visibility instead.


<!-- keywords: Controller, Input, input -->
### Controller input is visible to ALL windows, including unfocused windows
Unlike keyboard/mouse input (which requires the game window to be focused), controller/gamepad inputs are received by ALL running applications including unfocused ones. Enable the project setting `input_devices/joypads/ignore_joypad_on_unfocused_application` (or set `Input.ignore_joypad_on_unfocused_application = true` at runtime) to prevent accidental input while the player interacts with another window. Also:
- Holding a controller button does NOT generate 'echo' events (keyboard holding does) — implement echo manually with a Timer if needed
- Controller inputs do NOT prevent screen sleep/power saving — check 'Display > Window > Energy Saving > Keep Screen On'
- **Windows: max 4 simultaneous controllers** (XInput API limitation)


<!-- keywords: Input, Vector2, input, action -->
### Input.get_vector() gives circular deadzone; manual Vector2 subtraction gives square deadzone
Use `Input.get_vector(neg_x, pos_x, neg_y, pos_y)` for joystick/WASD movement — it normalizes the result and applies a **circular deadzone** (correct behavior, prevents drift at diagonal). Manual construction `Vector2(get_action_strength(...), get_action_strength(...)).limit_length(1.0)` uses a **square deadzone**, which causes asymmetric sensitivity near diagonal edges. The optional 5th parameter to `get_vector()` overrides the per-action deadzone average.


<!-- keywords: NavigationServer3D, physics, navigation, pathfinding, await -->
### NavigationServer3D does NOT sync on the first frame — must await physics_frame
On the very first frame, NavigationServer3D's map has not yet synchronized region data. Any path query called in `_ready()` will return an empty path. Pattern to fix:
```gdscript
func _ready():
    actor_setup.call_deferred()
func actor_setup():
    await get_tree().physics_frame  # Wait for NavigationServer sync
    navigation_agent.set_target_position(target_pos)
```
Also: NavigationObstacle3D does **NOT** affect pathfinding — it only constrains avoidance velocity. To block pathfinding, modify navigation meshes instead.


<!-- keywords: Compute, Forward, Mobile, Compatibility, Shader, shader, rendering -->
### Compute shaders only work with Forward+ or Mobile renderer, not Compatibility
Compute shaders require `RenderingServer.create_local_rendering_device()` which only works on the **Forward+** or **Mobile** renderer. The Compatibility renderer does not support compute shaders. Additionally:
- Compute shader support is **poor on mobile devices** due to driver bugs, even if technically supported
- Local RenderingDevice instances **cannot be debugged with RenderDoc**
- RIDs created via RenderingDevice are **NOT freed automatically** — you must call `rd.free_rid()` on each buffer, pipeline, and uniform set
- `rd.sync()` blocks the CPU until GPU finishes — for performance, wait **2-3 frames** before calling sync
- Long GPU computations (5-10 seconds) trigger **Windows TDR** (driver crash/reset) — split into multiple dispatches


<!-- keywords: AudioSample, AudioEffects, AudioStreamPlayer3D, Doppler, Reverb, Web, audio -->
### AudioSample does NOT support AudioEffects; AudioStreamPlayer3D Doppler/Reverb not on Web
AudioSample (used for sample-based playback) does not support any AudioEffect bus effects. Only AudioStream-based playback supports effects. Also:
- AudioStreamPlayer3D's **Reverb Bus** and **Doppler** features are NOT supported on Web when playback mode is **Sample** (the default for Web). Switch to Stream mode to enable them, at the cost of higher latency unless threads are enabled.
- Doppler tracking must be explicitly enabled on BOTH the AudioStreamPlayer3D **and** the Camera3D nodes. Set tracking to **Idle** for objects moved in `_process()`, or **Physics** for objects moved in `_physics_process()`.
- The old `AudioEffectLimiter` is deprecated — use `AudioEffectHardLimiter` instead.


<!-- keywords: HTTPRequest, Node, add_child, network -->
### HTTPRequest node: only ONE request at a time — create multiple nodes for parallel requests
An HTTPRequest node can only handle one in-flight request. A second call to `request()` while a request is pending will fail. Strategy for parallel requests: create and delete HTTPRequest nodes at runtime as needed (`var req = HTTPRequest.new(); add_child(req); ... req.queue_free()`). Also: Android exports require **Internet** permission enabled in the export preset, or all network communication will be silently blocked.


<!-- keywords: WorldEnvironment, EDITOR, ONLY, scene, Environment, Tree -->
### WorldEnvironment: only ONE allowed per scene tree; preview sky is EDITOR-ONLY
Only one WorldEnvironment node can exist in the active scene tree — adding more generates a warning and the extras are ignored. The **preview environment and sun** shown in the editor's 3D viewport is ONLY visible in the editor, NOT in the running project. If no WorldEnvironment or DirectionalLight3D is in the scene at runtime, the sky will be black. Use the 'Add Sun to Scene' / 'Add Environment to Scene' buttons in the preview dialog to add them as real nodes. Also: the background sky affects ambient and reflected light on all objects even when the sky is never directly visible on screen.


<!-- keywords: NavigationAgent, NavigationAgent2D, navigation, avoidance, pathfinding -->
### NavigationAgent: avoidance needs target_position set, and never call get_next_path_position after finish
Non-obvious NavigationAgent pitfalls:
- `get_next_path_position()` must be called **once per `_physics_process`** until `is_navigation_finished()` returns true. Calling it AFTER the target is reached triggers repeated path updates and causes the agent to jitter/dance in place.
- NavigationAgent avoidance requires a `target_position` to be set **even when only using avoidance** — without it, `velocity_computed` always emits a zero vector.
- RVO avoidance fails in perfectly symmetric scenarios (agents moving directly toward each other at equal speeds) — agents can't determine which side to pass on.
- 2D avoidance (xz plane) and 3D avoidance (xyz) run in **separate simulations** — agents cannot affect agents in the other mode.
- NavigationObstacle3D affects avoidance ONLY, not pathfinding. To block pathfinding, change the navigation mesh.


<!-- keywords: CanvasModulate, DirectionalLight2D, canvas, light, 2D -->
### 2D lights: background color is unlit; CanvasModulate required; DirectionalLight2D shadows are infinitely long
Non-obvious 2D lighting gotchas:
- The **background color** (project background) does NOT receive 2D lighting. Add a Sprite2D for the background to light it.
- Without **CanvasModulate**, 2D lights only add brightness to the already-fully-lit unshaded scene (which looks fully bright). Add a CanvasModulate with a dark color to define the 'ambient darkness' that lights reveal.
- DirectionalLight2D shadows are **always infinitely long** regardless of the Height property — this is a rendering limitation.
- 2D light/shadow rendering uses the **Viewport's pixel resolution**, not sprite texel resolution. Nearest (pixel) texture filtering will NOT make shadows pixelated. Use a custom shader to snap LIGHT_VERTEX to a pixel grid.
- Enabling **normal maps** on 2D sprites makes lights appear weaker (because the surface angle now matters). Increase the light's **Height** property to compensate.
- PointLight2D **Offset** changes the position of the light texture but does **NOT** move the shadow source — only moving the node moves shadows.


<!-- keywords: In, TOPMOST, BEHIND, scene, Node, int -->
### In the scene panel, TOPMOST nodes draw BEHIND lower nodes (counterintuitive Z order)
In Godot's 2D scene tree, the node listed **first (topmost)** in the Scene panel is rendered **behind** nodes listed later (lower in the list). The last child in the panel draws on top. This is opposite to many other tools. Use the node's `z_index` property or `CanvasItem > Z Index` for explicit depth control. CanvasLayer nodes are independent of scene tree order — they draw based on their `layer` number only, not their position in the tree.


<!-- keywords: SubViewport, audio, rendering, Viewport, Camera2D, Camera3D, Camera, Input, input -->
### SubViewport: no automatic input; needs audio listener enabled; camera renders to nearest parent viewport
SubViewport non-obvious behavior:
- SubViewports do **not** receive input automatically unless they are a direct child of a **SubViewportContainer**.
- To hear positional 3D or 2D audio from a SubViewport, enable **Audio Listener 2D/3D** on the SubViewport.
- A Camera3D/Camera2D always renders to the **closest parent Viewport** in the node hierarchy — not the root viewport. Cameras inside a SubViewport subtree render to that SubViewport, not to the screen.
- Only **one camera can be active per Viewport**. With multiple cameras, call `camera.make_current()` or set the `current` property.


<!-- keywords: Control, Containers, MarginContainer, Node, UI, theme, Theme, Container, VBoxContainer, HBoxContainer -->
### Control nodes inside Containers give up manual positioning; MarginContainer margins are theme values
UI Container non-obvious behavior:
- When a **Control** node is a child of a **Container**, it gives up its own position/size control. Any manual `position` or `size` changes will be overridden or ignored the next time the container reflows.
- **MarginContainer** margins are **Theme** constants (not direct properties) — edit them via the node's **Theme Overrides > Constants** section.
- **ScrollContainer** only accepts a **single child node** (wrap multiple children in a VBoxContainer/HBoxContainer first).
- Custom containers override the **NOTIFICATION_SORT_CHILDREN** notification and must call `queue_sort()` to trigger a re-layout when settings change.


<!-- keywords: Stretch, Mode, Disabled, Scale, int -->
### Stretch Mode and resolution: 'Disabled' = 1:1 pixels; for pixel art use 'integer' Scale Mode
Project Settings > Display > Window > Stretch settings:
- **Disabled** (default): 1 game unit = 1 pixel. No stretching. Best for tools and apps with explicit sizing.
- **Canvas Items**: Scene rendered at target resolution, base size scaled to fill screen. 3D is unaffected. May cause 2D scaling artifacts.
- **Viewport**: Scene first rendered at base resolution (lower quality), then the result is scaled to fit screen. Best for pixel art quality.
- **Stretch Scale Mode = Integer** (Godot 4.2+): Rounds scale factor DOWN to the nearest integer — prevents sub-pixel distortion for pixel art.
- For both portrait and landscape support: use a **square** base resolution (e.g., 720×720) instead of a rectangle.
- `GUI > Theme > Default Theme Scale` project setting **cannot be changed at runtime** — it's read once at startup.


<!-- keywords: Windows, Exclusive, Fullscreen, Input, input -->
### Windows: use 'Exclusive Fullscreen' not 'Fullscreen' for lower jitter and input lag
Godot's **Fullscreen** window mode intentionally leaves a 1-pixel gap at the bottom so Windows does NOT treat it as exclusive fullscreen. This prevents Windows from automatically enabling per-pixel transparency features, but it DISABLES Windows' game-specific optimizations. **Exclusive Fullscreen** uses the actual screen size and allows Windows to reduce jitter and input lag for games. Note: physics interpolation improves visual smoothness but INCREASES input lag for physics-dependent behavior (player movement) — compensate by increasing Physics Ticks Per Second.


<!-- keywords: Compatibility, Mesh, mesh, Shader, shader, rendering -->
### Compatibility renderer: shader stutter requires pre-warming all meshes for 1 frame during loading
Forward+ and Mobile renderers (Godot 4.4+) use an ubershader approach to minimize shader compilation stutter. The **Compatibility** renderer (OpenGL-based) cannot use this approach and will stutter the FIRST time any new mesh/material/particle is rendered. Workaround: during level loading, spawn ALL meshes and visual effects in front of the camera for **one frame** (hidden behind a fullscreen UI/ColorRect) to force shader compilation before gameplay starts.


<!-- keywords: Not, Ogg, Vorbis, Theora, audio -->
### Not all .ogg files are Ogg Vorbis audio — Theora video also uses .ogg extension
Some `.ogg` files contain Ogg **Theora** video or Ogg **Opus** audio, not Vorbis. Loading Theora video as `AudioStreamOggVorbis` will fail. Use `VideoStreamTheora` for video, `AudioStreamOggVorbis.load_from_file()` for Vorbis audio. Also: `VideoStreamPlayer.autoplay` will NOT trigger if the stream property is empty when the node enters the tree — always call `play()` manually after setting `stream`. When loading glTF from a buffer (not a file), you must set `GLTFState.base_path` manually so external textures can be found. And when loading runtime images to display on 3D surfaces, call `image.generate_mipmaps()` to prevent grainy appearance at distance.


<!-- keywords: All, GDScript, EditorPlugin, gdscript, @tool, editor -->
### All GDScript files in an EditorPlugin must be @tool, not just the main plugin script
When creating an EditorPlugin, the main plugin script MUST have `@tool` and `extends EditorPlugin`. But any **other** GDScript files that the plugin uses (helper classes, custom nodes, utilities) must ALSO have `@tool` at the top. GDScript files without `@tool` that are used by the editor plugin will **behave as empty files** — no functions will run. Additionally: C# EditorPlugin scripts require the project to be built first before the plugin can be enabled in the Plugins tab.


<!-- keywords: READ, ONLY, Godot, export -->
### res:// is READ-ONLY in exported projects; user:// default path is buried in Godot's data folder
In exported (non-editor) projects, `res://` is read-only. NEVER try to write files to `res://`. Always use `user://` for saves, config, and user data. Non-obvious `user://` behavior:
- **Default location**: `%APPDATA%/Godot/app_userdata/[project_name]` (Windows) — buried inside Godot's editor data, NOT the standard application data folder.
- To put saves in the standard location (`%APPDATA%/[project_name]`), enable `application/config/use_custom_user_dir` in Project Settings.
- **HTML5**: `user://` maps to a virtual filesystem stored via **IndexedDB** in the browser, NOT real disk files.
- To convert `res://` or `user://` paths to absolute OS paths (e.g. for OS.shell_open()), use `ProjectSettings.globalize_path()`.


<!-- keywords: JSON, Vector2, Vector3, Color, Rect2, Quaternion, serialization -->
### JSON serialization can't handle Vector2, Vector3, Color, Rect2, Quaternion — use binary for complex state
GDScript's JSON.stringify() only supports: int, float, String, bool, Array, Dictionary, null. It does NOT support Godot-specific types. Must manually convert them:
- Vector2/3 → Dictionary {"x":..., "y":...}
- Color → {"r":..., "g":..., "b":..., "a":...}
Alternative: use `FileAccess.store_var()` / `get_var()` for **binary serialization** which handles ALL common Godot types, produces smaller files, and requires no custom encoding.
Warning: when saving nested Persist objects (both parent and child saved), **NodePaths become invalid** on load. Save in stages: load parent objects first, then resolve child references.


<!-- keywords: FRAMERATE, DEPENDENT, delta, _process, _physics_process -->
### lerp(a, b, delta * speed) is FRAMERATE-DEPENDENT — use exponential formula for framerate independence
The common 'smooth follow' pattern `position = position.lerp(target, delta * speed)` is **framerate-dependent** because the weight parameter is a percentage of remaining distance, not an absolute amount. At higher frame rates, more lerp steps occur per second, reaching the target faster. For true framerate independence:
```gdscript
# Framerate-independent exponential decay smooth follow:
var weight = 1.0 - exp(-speed * delta)
position = position.lerp(target, weight)
```
In `_physics_process()`, `delta` is nearly constant (fixed physics rate), so the simple form usually works fine. In `_process()` (variable delta), always use the exponential formula.


<!-- keywords: VoxelGI, Forward, VoxelGIData, Mode, Static, Mesh, MeshInstance3D, mesh, static -->
### VoxelGI: Forward+ only; save VoxelGIData as .res not .tres; mesh must have GI Mode = Static
VoxelGI is supported **only** in the Forward+ renderer — it does NOT work in Mobile or Compatibility. Before baking:
- Static geometry must be imported with **Light Baking** set to **Static** or **Static Lightmaps** in the Import dock, OR have **Global Illumination > Mode = Static** set in the MeshInstance3D inspector.
- Always save the VoxelGIData resource as an **external .res file** (binary resource), not .tres (text). Using text format embeds large binary data in the scene file, making it huge and slow to load.


<!-- keywords: LightmapGI, light -->
### LightmapGI: only bakes siblings/children; UV2 slot reserved; no reflections included
Non-obvious LightmapGI limitations:
- LightmapGI **only bakes nodes that are siblings or children** of the LightmapGI node — it does NOT bake the entire scene. Use multiple LightmapGI nodes for different areas.
- After baking, the **UV2 slot is reserved** for lightmap data. You cannot use UV2 for other purposes in materials or custom shaders on those meshes.
- LightmapGI does **NOT** provide reflections — you must also add **ReflectionProbe** nodes for interiors or use a Sky for exteriors.
- Lightmap baking is **NOT supported in the web editor** (use desktop editor to bake, then render the result on web).
- For GridMap lightmaps: the lightmap texel size setting must be changed BEFORE converting the scene to a MeshLibrary — it cannot be changed afterward.


<!-- keywords: MeshLibrary, MeshInstance3D, scene, Node, Material, material, Mesh, mesh -->
### MeshLibrary: materials must be in the mesh's slots, not MeshInstance3D node slots
When creating a MeshLibrary scene for GridMap:
- Materials must be assigned in the **mesh resource's material slots** (via the Mesh property), **NOT** in the MeshInstance3D node's material override slots. Node materials are **ignored** during MeshLibrary export.
- Each MeshLibrary item's root must be a MeshInstance3D — only the visual mesh is exported. Allowed children: one StaticBody3D (for physics), one NavigationRegion3D (for navmesh). Other node types are silently ignored.
- GridMap is NOT a general-purpose 'nodes on a grid' system — it places optimized meshes only.


<!-- keywords: Texture2DArray, CubemapArray, Texture, texture, Shader, shader, 2D -->
### Texture2DArray and CubemapArray cannot be displayed without a custom shader
Image import type gotchas:
- **Texture2DArray** and **CubemapArray**: Godot's built-in 2D/3D shaders cannot display these — you must write a custom shader to sample and display them.
- **SVG**: Text elements in SVG files must be **converted to paths** before importing, or they will not appear in the rasterized result.
- **JPEG**: Does NOT support transparency. Use PNG or WebP for transparent images.
- **PNG**: Limited to **8 bits per channel** — no HDR support. Use .exr or .hdr for HDR images.
- **Textures stored in video memory**: Pixel data cannot be read back directly from CPU. Convert to Image first using `texture.get_image()` if you need CPU access.


<!-- keywords: Mesh, mesh, 3D, import -->
### 3D import name suffixes: -col keeps mesh; -colonly removes mesh; -loop needs no hyphen
3D scene name suffixes (case-insensitive, accept -, $, or _ as separator):
- `-col`: add trimesh StaticBody child, **keep visual mesh**
- `-colonly`: add trimesh StaticBody child, **REMOVE visual mesh** (for collision-only geometry)
- `-convcol`: add convex CollisionShape child, keep visual mesh
- `-convcolonly`: add convex CollisionShape, remove visual mesh
- `-navmesh`: convert to NavigationMesh, REMOVE visual mesh
- `-noimp`: remove the entire node on import
- `-occ`: keep mesh AND add OccluderInstance3D
- `-occonly`: remove mesh, add OccluderInstance3D only
- `-rigid`: make the node a RigidBody3D
- `-vehicle` / `-wheel`: wrap in VehicleBody3D / VehicleWheel3D
Animation loop: naming clips with **'loop' or 'cycle'** (prefix or suffix, NO hyphen required) imports them as looping. Note: in Blender NLA Editor, action must be named with loop/cycle prefix or suffix.


<!-- keywords: Audio, audio, light -->
### Audio format trade-offs: WAV = lightest CPU; OGG = smallest files; MP3 = mobile balance
Audio format selection guide:
- **WAV**: Lightest CPU usage (hundreds of simultaneous voices fine). Largest files. Best for short/repetitive sound effects.
- **Ogg Vorbis**: Smallest file size, highest CPU decoding cost. Best for music, long speech, ambient sounds.
- **MP3**: Less CPU than Ogg Vorbis, larger files than Ogg. Best for mobile/web where CPU is limited but multiple streams needed simultaneously.
For WAV import: use Trim to remove silence, Loop to enable looping, and Force Mono to halve file size on mono sounds. Prefer 22050 Hz for voices (indistinguishable from 44100 Hz for speech).


<!-- keywords: TileMap, Godot, TileMapLayer, TileSet, Node, Resource, tile, int -->
### TileMap in Godot 4.3+: TileMap split into TileMapLayer nodes; share TileSet as external resource
Since Godot 4.3, the old TileMap node was replaced by individual **TileMapLayer** nodes:
- Each layer is a separate TileMapLayer node — you can only place **one tile per position per layer**. Use multiple TileMapLayer nodes to overlap tiles (e.g., background + objects + foreground).
- To share the same TileSet across multiple levels/scenes: save the TileSet as an **external .tres resource** (click dropdown next to TileSet property > Save). Built-in TileSet is not shareable.
- Y-Sort on TileMapLayer: tiles are grouped by Y position instead of quadrants when enabled — this is required for top-down games where tiles overlap based on vertical position.


<!-- keywords: In, Godot, _ready -->
### In Godot 4.0+, randomize() is called automatically — don't call it in _ready()
Since Godot 4.0, the random seed is automatically set to a random value when the project starts. Calling randomize() in _ready() is unnecessary. If you need deterministic results, use seed(12345). To hash a string into a seed: seed("my_seed".hash()).


<!-- keywords: Use, Array, GDScript -->
### Use shuffle bags for non-repeating randomness; Array.pick_random() has no repeat prevention
Array.pick_random() can return the same element consecutively. For uniform non-repeating distribution, use the shuffle bag pattern:
```gdscript
# Shuffle bag: each item appears once before repeating
var _bag = []
var _full = ["a", "b", "c", "d"]

func _ready():
    _bag = _full.duplicate()
    _bag.shuffle()

func get_next():
    if _bag.is_empty():
        _bag = _full.duplicate()
        _bag.shuffle()
    return _bag.pop_front()
```
rand_weighted() on RandomNumberGenerator returns an INDEX into the weights array, not a value.


<!-- keywords: NavigationObstacle, ENABLED, navigation, navmesh, avoidance, Mesh, mesh -->
### NavigationObstacle avoidance is ENABLED by default — disable it if using only for navmesh baking
NavigationObstacle2D/3D has avoidance_enabled = true by default. If you only want it to affect navigation mesh baking (affect_navigation_mesh=true), set avoidance_enabled=false to save performance. For nav mesh baking, obstacles only REMOVE geometry from the navmesh — they never add geometry.


<!-- keywords: Static, NavigationObstacles, navigation, static -->
### Static NavigationObstacles (polygon vertices) can't move cheaply; use radius (dynamic) for moving obstacles
- **Static obstacle** (vertices set): Acts as hard boundary for 2D avoidance mode only. Cannot be moved without a full rebuild (expensive). Warping a static obstacle on top of agents gets them stuck.
- **Dynamic obstacle** (radius > 0): A 'please move away' soft repulsion. Can update position every frame with no extra cost. Set velocity to allow agents to predict movement.
- Static obstacles only affect agents using 2D avoidance mode; 3D avoidance uses the radius (sphere) form.


<!-- keywords: StandardMaterial3D, Disabled, Texture, texture, Material, material -->
### StandardMaterial3D: transparency is Disabled by default; transparent texture is invisible unless you change it
In Godot, materials are fully opaque by default. If your albedo texture has transparent pixels, they won't show until you change Transparency mode:
- **Alpha**: Full transparency, but no shadow casting, not in SSR, not in most reflections. Slower to render.
- **Alpha Scissor**: Hard cutout (all-or-nothing). Casts shadows. Best for foliage/fences.
- **Alpha Hash**: Dithered cutout. Casts shadows. Best for hair.
- **Depth Pre-Pass**: Mixed opaque + alpha blend. Mostly correct sorting. Casts shadows.
Also: Blender exports materials with Backface Culling DISABLED by default — enable it in Blender before exporting to avoid wasted GPU rendering backfaces in Godot.


<!-- keywords: ReflectionProbe, Mobile, Compatibility, Mesh, mesh -->
### ReflectionProbe: Mobile limit is 8 per mesh, Compatibility limit is 2 per mesh
- Forward+: 512 clustered elements limit in view (probes share this budget with lights/decals)
- Mobile: only 8 ReflectionProbes applied per mesh resource
- Compatibility: only 2 ReflectionProbes applied per mesh resource
- ReflectionProbe.max_distance controls what objects appear INSIDE the reflection (not the probe's own visible range)
- For probe blending: extents of neighboring probes must OVERLAP (only a few units needed)
- Enable Shadows on a probe in 'Always' update mode is very expensive; leave disabled unless necessary


<!-- keywords: Volumetric, Forward, ONLY, FogVolumes, SUBTRACT, global -->
### Volumetric fog is Forward+ ONLY; FogVolumes can ADD or SUBTRACT from global fog density
Volumetric fog is not available in Mobile or Compatibility renderers. FogVolumes affect fog density in a local area:
- Set FogVolume density positive to ADD fog in an area
- Set FogVolume density negative to REMOVE/subtract fog in an area (e.g., clear zones inside buildings)
- Volumetric fog emission adds ambient color but does NOT cast light on other surfaces
- Set Density=0 globally to have ONLY FogVolumes visible (local fog without global fog)


<!-- keywords: Physical, DoF, Camera -->
### Physical camera units force GPU depth-of-field even if DoF visually looks minimal
Enabling physical light/camera units (Rendering > Lights And Shadows > Use Physical Light Units) enforces depth of field on the GPU even when DoF is subtle, causing a moderate performance hit. If using physical units, assign CameraAttributes to WorldEnvironment too, or the 3D viewport appears extremely bright with DirectionalLights.


<!-- keywords: Decals, Compatibility, Normal, Albedo, Mix, rendering -->
### Decals are NOT supported in Compatibility renderer; Normal/ORM-only decals need Albedo Mix=0
- Decals require Forward+ or Mobile renderer. For Compatibility renderer, use Sprite3D instead.
- Decal Y-axis extent controls projection LENGTH — keep as short as possible for better culling performance.
- Normal Fade > 0 prevents projecting on non-facing surfaces (useful for floors/walls). 0 = projects on everything.
- For footsteps/wet puddles (normal+ORM only): set Albedo Mix = 0.0. Setting it to 0 doesn't reduce ORM/normal impact.


<!-- keywords: Visibility, AABB, CENTER, Begin, End, Margin, Disabled -->
### Visibility range distances are from AABB CENTER, not surface; use Begin/End Margin with Disabled fade for hysteresis
Visibility Range is configured per-GeometryInstance3D node:
- Begin/End values are measured from the AABB center of the object, not from mesh surface
- Fade Mode 'Disabled' + margins = hysteresis (instant snap, prevents ping-pong at LOD boundary). Best performance.
- Fade Mode 'Self' or 'Dependencies' = alpha blend transition. Looks smoother but forces transparent rendering (perf cost).
- Can mix node types in LOD: MeshInstance3D near, Sprite3D impostor far — set visibility ranges accordingly


<!-- keywords: Parallax2D, Texture, texture -->
### Parallax2D: textures must have top-left at (0,0), NOT centered — centering breaks infinite repeat
Using Parallax2D (preferred over deprecated ParallaxBackground/ParallaxLayer):
- The 'infinite repeat canvas' starts at (0,0) and expands DOWN and RIGHT. Centering textures at origin causes partial repeat.
- repeat_size must match your TEXTURE size (not viewport size). Use scroll_offset to start at a different position.
- If camera zoom changes (Camera2D.zoom != 1), increase repeat_times so texture still covers screen when zoomed out.
- For split-screen: duplicate parallax nodes in each SubViewport; use visibility_layer + Viewport canvas_cull_mask to isolate them.


<!-- keywords: SpringArm3D, Camera3D, DIRECT, collision, Camera, camera, Node3D, RayCast -->
### SpringArm3D: camera collision shape only works if Camera3D is DIRECT child; otherwise falls back to inaccurate raycast
For third-person cameras:
- SpringArm3D uses the Camera3D near-plane pyramid as its collision shape ONLY when Camera3D is a direct child of SpringArm3D.
- If a custom shape is assigned, that shape is ALWAYS used (overrides camera shape).
- If no shape AND camera is not a direct child, falls back to a single ray cast — inaccurate for camera collisions.
- Standard setup: Node3D (pivot) → SpringArm3D → Camera3D. Move pivot up so it's not at ground level.


<!-- keywords: COMPILE, TIME, PackedScene, scene, Node, Resource, load, preload, Mesh, mesh -->
### preload() is COMPILE-TIME only (constant path required); load() is runtime (variable path OK); PackedScene.instantiate() creates new nodes but still shares embedded image/mesh resources
- preload() is resolved at parse time — path MUST be a string literal constant, not a variable
- load() is resolved at runtime — can use a variable path, string expressions, etc.
- Scenes (PackedScene) are also cached; instantiate() creates new node trees but sub-resources (meshes, textures) inside the scene file are still shared — call duplicate(true) on a resource if you need a per-instance copy


<!-- keywords: Screen, ONCE, Node, Texture, texture, Shader, shader, 2D -->
### Screen-reading shaders: in 2D screen texture copied only ONCE; overlapping nodes won't see each other
When hint_screen_texture is used in 2D, Godot copies the screen once for the first node that needs it. Subsequent overlapping nodes share the same copy — they won't see the first node's output. This causes the 'disappearing object' effect.
In 3D, screen texture is captured AFTER opaque geometry pass but BEFORE transparent pass — transparent objects are not in the screen texture.
textureLod(screen_texture, uv, LOD > 0) requires a mipmap-enabled filter (filter_nearest_mipmap or similar); with filter_nearest, LOD > 0 has no effect.


<!-- keywords: Pausing, physics, signal, connect, emit, Node, _process, _physics_process, input -->
### Pausing: signals still fire on paused nodes; physics servers stop even for PROCESS_MODE_ALWAYS nodes
When get_tree().paused = true:
- SIGNALS still emit and call connected functions, even on paused nodes
- Physics servers are SHUT DOWN globally — even nodes with PROCESS_MODE_ALWAYS won't get physics (collisions, move_and_slide, etc.)
- Animation/audio/particles auto-pause and auto-resume
- For pause menu: set root node of menu to PROCESS_MODE_WHEN_PAUSED; all children inherit it
- To resume physics while paused: PhysicsServer2D.set_active(true) / PhysicsServer3D.set_active(true)


<!-- keywords: Autoload, BEFORE, scene, autoload, singleton -->
### Autoload is NOT a true singleton — it can be instantiated multiple times; added to root BEFORE any scene loads
Godot Autoloads are added to the root viewport before any game scene. They persist across scene changes. But Godot does NOT enforce a singleton — the user can still instantiate the class manually.
In C#, the 'Enable' column in Project Settings > Autoload has NO effect — for C# autoloads, create a static Instance property and assign it in _Ready().


<!-- keywords: export, property, @tool -->
### @export requires type specifier OR constant default; setters/getters only fire when script is @tool in editor
- `@export var x` requires either a type annotation OR a constant/literal default, or it will error
- In the editor, exported properties are editable even if the script is not @tool, BUT setters/getters won't be called unless the script has the @tool annotation
- Groups cannot be nested — use @export_subgroup for sub-groups inside @export_group
- @export_category breaks the automatic inheritance-based property grouping — use carefully


<!-- keywords: Node, Tree, delta -->
### remove_child() stops processing and groups but keeps node in memory; data goes stale without delta/tree access
Scene management strategies when changing scenes:
- **change_scene_to_file/packed**: Deletes current scene immediately. Memory freed, processing stops. Must reload to return.
- **hide()**: Node invisible but still processes. Memory used. Data stays fresh. Good for quick returns.
- **remove_child()**: Processing stops, memory kept. BUT the node loses access to groups, SceneTree signals, delta time — data can go stale.
To preserve data across scene changes without an Autoload: re-attach the data node to a persistent parent before changing scenes.


<!-- keywords: Input, input -->
### _input() receives ALL events first; _unhandled_input() only gets events NOT consumed by _input() or UI
Input processing order:
1. _input() — receives every event before anything else. Call get_viewport().set_input_as_handled() to consume it.
2. GUI controls (_gui_input) — consume UI interactions
3. _unhandled_input() — receives events not consumed by _input() or UI. Use this for gameplay input to avoid conflicts with UI.


<!-- keywords: Spatial, RECEIVING, Shader, shader, particles -->
### Spatial shader: 'shadows_disabled' blocks RECEIVING shadows, NOT casting; 'fog_disabled' needed for blend_add particles
Spatial shader render mode gotchas:
- `shadows_disabled`: the mesh stops RECEIVING shadows from lights, but still CASTS shadows onto other surfaces
- `specular_disabled`: disables direct light specular highlights only. For no reflections at all, also set SPECULAR = 0.0 in fragment()
- `fog_disabled`: prevents fog from affecting the material. Essential for blend_add particles — otherwise fog multiplies incorrectly
- Stencil buffer read is only possible in the TRANSPARENT pass. Stencil reads in the opaque pass silently fail.


<!-- keywords: In, Godot, DOWN, Transform2D, int, 2D -->
### In Godot 2D, Y-axis points DOWN; Transform2D.x = first column = X axis vector, t.x.y = bottom-left of matrix
Coordinate system reminders:
- 2D: Y increases DOWNWARD (opposite of math textbooks). Rotations are clockwise for positive values.
- Transform2D stores basis as column vectors: t.x = X axis column, t.y = Y axis column, t.origin = translation
- t.x.y means Y component of X column = bottom-left of the 2x2 basis. t.y.x = top-right. t.x.x = top-left.
- 3D: Y is up by default. Forward direction is -Z in Godot's right-handed system.


<!-- keywords: GDScript, LEFT, AudioStreamPlayer, audio, gdscript, debug, assert -->
### GDScript: ** operator is LEFT-associative; assert() ignored in non-debug builds; rename bus = AudioStreamPlayer loses it
GDScript operator gotchas:
- `**` (power) is LEFT-associative like C++ (unlike Python): `2 ** 2 ** 3` = `(2**2)**3 = 64`, NOT 256. Use parentheses to clarify intent.
- `assert(condition)` is completely IGNORED in exported (non-debug) builds — don't rely on it for production logic
- `%` operator behaves like C++ (can return negative for negative dividends), not like Python
- `!`, `&&`, `||` work but are not idiomatic — prefer `not`, `and`, `or`


<!-- keywords: AudioStreamPlayer, NAME, Master, audio -->
### AudioStreamPlayer bus reference is by NAME — renaming a bus silently reroutes to Master
Audio bus management gotchas:
- AudioStreamPlayers reference buses by NAME. If a bus is RENAMED, the player silently falls back to Master.
- Audio buses auto-disable after a few seconds of silence (VU turns blue). No need to manually disable silent buses.
- Bus routing always flows RIGHT to LEFT to prevent feedback loops. You cannot route a bus to a bus that is to its right.
- Audio effects on web require playback mode = Stream (not the default Sample mode).
- Default bus layout is saved to res://default_bus_layout.tres automatically.


<!-- keywords: For, Time, AudioStreamPlayer, audio -->
### For rhythm game audio sync: use Time.get_ticks_usec() + get_time_to_next_mix() + get_output_latency()
AudioStreamPlayer.play() has inherent latency. For precise sync:
```gdscript
var time_begin = Time.get_ticks_usec()
var time_delay = AudioServer.get_time_to_next_mix() + AudioServer.get_output_latency()
$Player.play()

func _process(delta):
    var time = (Time.get_ticks_usec() - time_begin) / 1000000.0
    time = max(0.0, time - time_delay)
```
Use Time.get_ticks_usec() — not accumulated delta — for accurate timing (delta accumulates floating point errors).


<!-- keywords: Compatibility, ONLY, scene, rendering, export -->
### Compatibility renderer is the ONLY option for web export; switching renderers may need manual scene adjustments
Renderer selection by platform:
- **Forward+**: Desktop only (default on desktop). Uses Vulkan/D3D12/Metal. Most features.
- **Mobile**: Mobile/desktop/XR. Uses Vulkan/D3D12/Metal. Good balance.
- **Compatibility**: Web/older devices. Uses OpenGL. REQUIRED for web export. Widest hardware support.
Switching Forward+ ↔ Mobile: minor tweaks needed. Switching to/from Compatibility: significant differences in lighting, shadows, effects.


<!-- keywords: GDScript, gdscript, int, float -->
### GDScript integer division: 5 / 2 == 2 (not 2.5); use posmod() for mathematical modulo with negative values
Division and modulo gotchas:
- `5 / 2` = 2 (integer division if both operands are int). Use `5 / 2.0` or `float(5) / 2` for float result.
- `%` only works on ints. For floats, use fmod().
- `%` and fmod() use TRUNCATION for negatives (like C++, unlike Python). `-7 % 3 == -1`, not 2. Use posmod(-7, 3) for mathematical modulo (= 2).
- Float comparison: never use `==` for floats. Use is_equal_approx(a, b) or is_zero_approx(a) instead.


<!-- keywords: Moving, OUTSIDE, Godot, Resource, FileSystem -->
### Moving/renaming files OUTSIDE Godot editor breaks all resource references; use only lowercase filenames
File system rules:
- ALWAYS move/rename/delete assets through Godot's FileSystem dock — doing it outside breaks all res:// references
- Windows/macOS are case-insensitive, but Linux/Android/web are NOT. 'MyFile.png' vs 'myfile.png' = works on Windows, breaks on Linux.
- Convention: use ONLY lowercase filenames and paths to prevent cross-platform issues
- Godot only supports `/` as path separator (even on Windows). Never use `\` in resource paths.


<!-- keywords: JavaScriptBridge, Godot, Array -->
### JavaScriptBridge callbacks must be stored as member variables; JS callback args arrive as single Godot Array
Web platform (JavaScriptBridge) gotchas:
- JavaScriptBridge.create_callback() returns a JavaScriptObject reference. You MUST store it as a member variable — if the reference is lost, the callback is garbage-collected by JavaScript.
- All arguments passed from JavaScript to a Godot callback arrive as a single Array parameter, not individual arguments.
- Base types (int, float, string, bool) are auto-converted between JS and Godot. Objects/arrays/functions become JavaScriptObjects.
- Floats may lose precision when crossing the JS↔Godot boundary.


<!-- keywords: Scaling, collision, physics, instantiate -->
### Scaling physics bodies or collision shapes is NOT supported — change extents instead; duplicate() before per-instance resize
Physics shape gotchas:
- Godot does NOT support scaling CollisionShape2D/3D nodes. Change the shape's extents/radius directly instead.
- Collision shape resources are SHARED by default. If you resize one, ALL instances using it change. Call .duplicate() on the shape resource before modifying.
- Cylinder collision shapes are buggy in GodotPhysics. Use box (most reliable) or capsule instead. Jolt physics handles cylinders better.
- Tunneling (fast objects passing through walls): enable Continuous CD, use thicker colliders, or increase physics tick rate (prefer multiples of 60: 120, 180, 240).


<!-- keywords: Physics, MUST, physics, _process, Node, delta, _physics_process, error, RayCast2D, RayCast3D, RayCast -->
### Physics space queries (raycast from code) MUST be done inside _physics_process(); accessing from _process() errors
Direct physics queries (raycasts, shape casts) via PhysicsDirectSpaceState must be called from _physics_process(), NOT _process() or _ready(). The physics space is LOCKED outside of physics steps.
```gdscript
func _physics_process(delta):
    var space = get_world_3d().direct_space_state
    var query = PhysicsRayQueryParameters3D.create(from, to)
    var result = space.intersect_ray(query)
    if result:
        print(result.position)
```
RayCast2D/RayCast3D nodes handle this automatically (results available every frame). Use code queries only when you need dynamic parameters.


<!-- keywords: Large, float, int -->
### Large world coordinates: single-precision float breaks at ~16 million units; objects vibrate far from origin
Single-precision floats lose integer precision beyond ±16,777,216. At large coordinates, objects vibrate/jitter because positions snap to nearest representable float value. Solutions:
- Enable large world coordinates in project settings (uses double-precision floats for physics)
- Origin shifting: periodically recenter the world around the player
- Large world coordinates mainly benefit 3D; 2D rendering doesn't improve with double precision.


<!-- keywords: Multiplayer, HTML5, network -->
### Multiplayer uses UDP only — forward UDP port (not just TCP); HTML5 has no raw TCP/UDP
Networking gotchas:
- Godot high-level multiplayer is UDP only (ENet). Router port forwarding must be UDP, not TCP.
- HTML5/Web platform: no raw TCP or UDP access. Only WebSocket and WebRTC are available.
- Multiplayer API uses SceneTree; any networked game must handle security (no server-side validation = exploitable).


<!-- keywords: CollisionShape, DIRECT, PhysicsBody, collision, physics, CollisionShape2D -->
### CollisionShape must be DIRECT child of PhysicsBody — indirect children (grandchildren) are silently ignored
CollisionShape2D/3D nodes must be immediate children of the PhysicsBody. If nested inside another node (e.g., a Node3D organizer), the shape won't be used for collision. Also, concave/trimesh shapes can ONLY be used with StaticBody (not CharacterBody or RigidBody unless frozen/static mode). Concave shapes have no 'volume' — objects can exist both inside and outside.


<!-- keywords: DIRECTION, CHANGES, move_and_slide, CharacterBody, velocity, collision, property -->
### move_and_slide() silently modifies the velocity property; get_slide_collision_count() only counts DIRECTION CHANGES
After calling move_and_slide(), the CharacterBody's velocity is automatically altered to reflect sliding/collision response. Do NOT re-assign velocity after the call if you want the slide result. Also, get_slide_collision_count() only counts collisions where the body changed direction, not every surface contact.


<!-- keywords: Jolt, Physics, DEFAULT, Godot, OPPOSITE, physics, RigidBody3D, int -->
### Jolt Physics is the default 3D engine for NEW projects since Godot 4.6; single-body joints behave OPPOSITE to Godot Physics
Jolt gotchas:
- Projects created in 4.6 or later use Jolt for 3D by default; older projects keep GodotPhysics until switched in Project Settings > Physics > 3D > Physics Engine. Many joint soft-limit properties (bias, softness, relaxation) are ignored.
- Single-body joints: Jolt assigns the body to node_b (Godot Physics uses node_a), giving INVERTED limits.
- Ray-cast face_index always returns -1 by default (opt-in setting, costs +25% memory for ConcavePolygonShape3D).
- Kinematic RigidBody3D contacts are NOT reported by default (opt-in via project setting).
- Collision margins work differently: Jolt shrinks shape then applies shell (size preserved but corners rounded).


<!-- keywords: GLSL, Godot, VERTEX, FRAGCOORD, Shader, shader -->
### GLSL to Godot shaders: VERTEX is model-space (auto-converted), UV Y is flipped vs FRAGCOORD, #include only .gdshaderinc
When porting GLSL/Shadertoy shaders to Godot:
- Godot VERTEX is model space; Godot handles model→clip conversion automatically (unlike gl_Position which is clip space)
- Shadertoy iResolution = 1.0 / SCREEN_PIXEL_SIZE in Godot
- UV y-coordinate is flipped compared to FRAGCOORD
- Shader #include only works with .gdshaderinc files (max depth 25)
- SCREEN_TEXTURE removed in Godot 4; use sampler2D with hint_screen_texture instead


<!-- keywords: CanvasItem, REPLACES, INVERTED, Shader, shader, canvas, light, 3D, 2D -->
### CanvasItem shader: defining light() REPLACES built-in lighting even if empty; 3D normal maps appear INVERTED in 2D
CanvasItem shader gotchas:
- If you define a light() function, it completely replaces the built-in light function, even if the body is empty. Use 'unshaded' render mode to skip lighting entirely.
- COLOR in fragment() already includes TEXTURE color multiplication. Writing `COLOR = COLOR;` is equivalent to an empty fragment() function.
- 3D normal maps appear inverted in 2D. Assign to NORMAL_MAP (not NORMAL directly) and Godot will convert automatically.
- TIME repeats every 3600 seconds (configurable). It's affected by time_scale but NOT by pausing.


<!-- keywords: Avoid, CollisionShapes, collision, physics, CollisionShape -->
### Avoid translating/rotating/scaling CollisionShapes at runtime — breaks physics engine broad-phase optimization
The physics engine's broad phase can efficiently discard inactive bodies only when StaticBody has a single non-transformed collision shape. Transforming shapes forces expensive narrow-phase checks against every shape. For dynamic objects, keep shapes at origin and adjust the PhysicsBody transform instead.


<!-- keywords: Transparent, Node3D, POSITION, rendering -->
### Transparent objects sorted by Node3D POSITION, not vertex position — overlapping transparent objects render in wrong order
Godot sorts transparent materials back-to-front using the Node3D's origin, NOT per-vertex depth. Overlapping transparent meshes will frequently sort incorrectly. Workarounds:
- Use material Render Priority to force ordering between specific materials
- Use VisualInstance3D.sorting_offset to shift sort depth
- Use Alpha Scissor instead of alpha blend for mostly-opaque textures (also faster)
- Transparent objects are NOT rendered to the normal-roughness buffer, so SSR/SSAO won't affect them.


<!-- keywords: Resolution, AXIS, Forward, ONLY -->
### Resolution scale is PER-AXIS: 0.5 scale = 4x fewer pixels, not 2x; FSR 1.0/2.2 are Forward+ ONLY
Resolution scaling gotchas:
- Scale is per-axis, so 0.5 renders 1/4 the pixels (not 1/2). Very low scales have outsized performance impact.
- FSR 1.0 and FSR 2.2 upscaling are ONLY available in Forward+ renderer.
- FSR2 provides its own TAA — the 'Use TAA' project setting is IGNORED when FSR2 is active.
- FSR Sharpness slider is INVERTED: 0.0 = sharpest, 2.0 = least sharp (default 0.2).
- Resolution scaling is NOT available for 2D. Simulate with viewport stretch mode.


<!-- keywords: ONLY, GPUParticles, CPUParticles, Shader, shader, GPUParticles2D, particles -->
### GPU particle shaders keep data from previous frame; ONLY work with GPUParticles, not CPUParticles
Unlike other shader types, particle shaders preserve output data between frames, enabling multi-frame effects. Custom particle shaders (start()/process() functions) only work with GPUParticles2D/3D nodes. CPUParticles simulate on CPU and can only use the built-in ParticleProcessMaterial properties. To use particle COLOR in a StandardMaterial3D, you must enable vertex_color_use_as_albedo.


<!-- keywords: Shader, EVERYWHERE, shader, GLSL, error -->
### Shader #define with trailing semicolon inserts semicolons EVERYWHERE it's used — causes silent syntax errors
A common shader preprocessor mistake:
```glsl
// BAD — the semicolon becomes part of the replacement
#define MY_COLOR vec3(1, 0, 0);
// Expands to: ALBEDO = vec3(1, 0, 0);; (double semicolon, or worse in expressions)

// GOOD — no trailing semicolon in #define
#define MY_COLOR vec3(1, 0, 0)
```
Also: #elifdef is NOT supported in Godot shaders. Use nested #ifdef/#else instead. Since 4.4, use CURRENT_RENDERER built-in define for renderer-specific shader code.


<!-- keywords: Physics, PAST, DISABLE, physics, Camera, mouse, int -->
### Physics interpolation shows objects 1-2 ticks IN THE PAST; camera mouse-look should DISABLE interpolation
Physics interpolation (disabled by default) smooths motion between physics ticks by interpolating between PREVIOUS and CURRENT tick transforms. This means visual positions lag 1-2 ticks behind actual physics state. Gotchas:
- Child with interpolation OFF still interpolates if PARENT has interpolation ON (interpolation is on local transform)
- For cameras: disable auto interpolation, use get_global_transform_interpolated() to follow target, handle mouse look in _process() without interpolation
- get_global_transform_interpolated() is expensive — use only for cameras, not everywhere in gameplay code


<!-- keywords: WebSocketPeer, MUST -->
### WebSocketPeer.poll() MUST be called every frame — no data transfer happens without it
WebSocketPeer requires calling poll() in _process() or _physics_process() for ANY data transfer or state updates. Without it, no packets are received and no state changes occur. Also:
- Android export requires INTERNET permission enabled in export preset
- Close code -1 means the disconnect was NOT properly notified by the remote peer


<!-- keywords: Depth, Godot, REVERSED, rendering -->
### Depth buffer in Godot 4.3+ uses REVERSED-Z: near plane is 1.0, far is 0.0; NDC Z-range differs by renderer
Post-processing shaders accessing depth_texture must account for reversed-Z. The full-screen quad vertex shader must use:
```glsl
POSITION = vec4(VERTEX.xy, 1.0, 1.0); // 1.0 for near plane (reversed-Z)
// NOT vec4(VERTEX, 1.0) which was the old pre-4.3 code
```
NDC Z-range: Forward+/Mobile (Vulkan) = [0.0, 1.0]; Compatibility (OpenGL) = [-1.0, 1.0]. Use CURRENT_RENDERER define for cross-renderer shaders.


<!-- keywords: MSAA, EDGES, TEXTURE, Texture, texture, sprite, Shader, shader, int, 2D -->
### 2D MSAA only affects geometry EDGES and sprite TEXTURE edges — not pixel art interiors, shaders, or fonts
2D MSAA increases coverage samples but NOT color samples. It will NOT antialias: pixel art texture interiors, custom 2D shader output, specular aliasing from Light2D, or font rendering. For Line2D, use the built-in Antialiased property instead (lower cost). MSAA 2D is a SEPARATE setting from MSAA 3D. MSAA 2D is Forward+/Mobile only (not Compatibility).


<!-- keywords: GPUParticles2D, Visibility, Rect, MUST, particles -->
### GPUParticles2D Visibility Rect MUST contain all particles or they disappear when offscreen
GPU particles use a Visibility Rect for culling. If particles travel outside this rect, they become invisible when the rect is offscreen even though particles would still be visible. Use 'Particles > Generate Visibility Rect' in toolbar to auto-calculate. Also: particles start with zero emissions — use Preprocess to pre-simulate seconds of particles before first frame.


<!-- keywords: Expression, REQUIRED, instantiate -->
### Expression class: ALL parameters are REQUIRED (no optional args); base_instance exposes ALL methods (security risk)
Unlike GDScript functions, Expression.execute() requires ALL parameters even if marked optional in docs. Setting base_instance to non-null allows the expression to call ANY method on that object — a security risk if users can input expressions. The Expression class works even without the GDScript module.


<!-- keywords: SoftBody3D, CollisionShape3D, Simulation, Precision, collision, physics, Mesh, MeshInstance3D, mesh -->
### SoftBody3D: NO CollisionShape3D child — collision derived from mesh; Simulation Precision below 5 causes collapse
SoftBody3D does NOT use CollisionShape3D children. Collision is derived directly from the mesh geometry. Gotchas:
- Simulation Precision below 5 causes the soft body to collapse into itself
- Pressure > 0 only works on CLOSED meshes — open meshes fly around uncontrollably
- Physics interpolation does NOT affect SoftBody3D nodes (only rigid/character bodies)
- Jolt physics significantly improves soft body behavior vs Godot Physics
- Soft body mesh must be linked to a MeshInstance3D — set the mesh path in the SoftBody3D inspector


<!-- keywords: JSON, Vector2, Vector3, Color, Rect2, Quaternion, ConfigFile, serialization -->
### JSON save/load does NOT preserve Vector2/Vector3/Color/Rect2/Quaternion — use ConfigFile or var_to_bytes() instead
JSON converts everything to basic types (string, number, bool, array, dict). Saving Vector2(1, 2) and loading it back gives a Dictionary, not a Vector2. Workarounds:
- Use ConfigFile which natively supports many Godot types
- Use var_to_bytes()/bytes_to_var() for full type preservation (binary format)
- Manually serialize: save as array [x, y], reconstruct on load with Vector2(arr[0], arr[1])
- ResourceSaver/ResourceLoader preserve all types and work with custom Resources


<!-- keywords: BLOCKS, Resource, load, Thread, threading -->
### load_threaded_get() BLOCKS main thread if resource isn't ready — always check status first
Background loading gotcha: calling load_threaded_get() before the resource finishes loading will BLOCK the main thread until it completes, defeating the purpose of threaded loading. Always check first:
```gdscript
var status = ResourceLoader.load_threaded_get_status(path)
if status == ResourceLoader.THREAD_LOAD_LOADED:
    var res = ResourceLoader.load_threaded_get(path)
elif status == ResourceLoader.THREAD_LOAD_FAILED:
    print('Load failed!')
# If IN_PROGRESS: wait more, show loading progress
```


<!-- keywords: MANIFOLD, Mesh, MeshInstance3D, mesh, CSG -->
### CSG meshes must be MANIFOLD (closed, watertight) — open meshes produce garbage results; CSG is prototyping-only
CSG (Constructive Solid Geometry) boolean operations require manifold (closed, watertight) meshes. Non-manifold meshes produce undefined behavior. Additional gotchas:
- CSG is NOT recommended for final game geometry — performance is poor vs static meshes
- Use 'Mesh > Bake Mesh' to convert CSG to a real MeshInstance3D for export
- CSGMesh3D wraps an existing Mesh resource in CSG, but requires the mesh to be manifold
- CSG nodes cannot use physics materials directly (bake first, then add StaticBody3D)


<!-- keywords: Scene, TileMapLayer, HIGHER, BEFORE, scene, Texture, texture, TileSet, tile, import, performance -->
### Scene tiles in TileMapLayer have HIGHER performance cost; set tile size BEFORE importing atlas texture
TileSet gotchas:
- Scene tiles (tiles that instance a scene) have significantly higher performance overhead than atlas tiles — each is instantiated as a separate scene
- Tile size in TileSet must be set BEFORE importing the atlas image. Changing tile size after import requires reimporting
- TileMapLayer uses a y-sort origin per tile for correct depth sorting — origin is at tile TOP by default in isometric mode
- Navigation baking via TileSet navigation layer requires NavigationRegion2D to be present in the scene


<!-- keywords: Self -->
### Self-contained mode: place '._sc_' file next to editor binary; user:// resolves to 'user_data/' subdir beside exe
When a file named `._sc_` or `_sc_` exists next to the Godot editor binary, the editor runs in self-contained mode:
- user:// path resolves to a 'user_data/' subdirectory beside the executable instead of AppData/Roaming
- Useful for portable installations (e.g., USB drives)
- The exported game also runs self-contained if `._sc_` is next to the game executable
- res:// is always read-only in exported games (packed PCK); always write to user:// instead


<!-- keywords: NEVER, Node3D, Euler, Basis, Quaternion, rotation -->
### NEVER use Node3D.rotation (Euler angles) in game logic — use basis vectors or quaternions instead
The `rotation` property exists for editor convenience and 2D parity, but the official Godot docs explicitly warn against using it in game code:
- Euler angle interpolation does NOT take the shortest path (e.g., 270°→0° may go the wrong direction)
- Gimbal lock causes unexpected direction reversals at certain orientations
- There's no unique way to decompose a rotation into Euler angles — results depend on application order
Use basis vectors instead: `-transform.basis.z` = forward, `transform.basis.y` = up, `transform.basis.x` = right.
Use quaternion slerp for smooth rotation interpolation.


<!-- keywords: Transform -->
### Transform precision degrades every frame when rotating — call orthonormalized() periodically (but it discards scale)
Successive rotation operations accumulate floating-point errors: axis lengths drift from 1.0, axes deviate from 90°. An object rotating every frame will eventually deform visibly. Fix:
```gdscript
# In _physics_process, occasionally:
transform.basis = transform.basis.orthonormalized()
```
WARNING: orthonormalized() silently discards any non-uniform scale applied to the basis. If you need scale, re-apply it after.
Also: Quaternion conversion (for slerp) silently drops scale information with no warning.


<!-- keywords: Sky, TIME, EVERY, Shader, shader -->
### Sky shader runs up to 6 times per frame; using TIME forces cubemap update EVERY frame
The sky shader executes for: full-res background, half-res pass, quarter-res pass, and all 6 faces of the radiance cubemap. Gotchas:
- Using TIME in a sky shader forces a radiance cubemap update EVERY frame — multiply your shader cost by 6+
- RADIANCE cubemap can ONLY be sampled during the background pass — sampling it during AT_CUBEMAP_PASS returns garbage
- ANY uniform change triggers a full radiance cubemap recalculation
- Use AT_CUBEMAP_PASS bool to skip expensive calculations: `if (!AT_CUBEMAP_PASS) { ... expensive stuff ... }`
- Set Sky.process_mode = PROCESS_MODE_REALTIME only when the sky actually changes at runtime


<!-- keywords: Fog, DENSITY, ALBEDO, EMISSION, Shader, shader -->
### Fog shader: DENSITY must be written or ALBEDO/EMISSION are completely ignored
FogVolume shader gotchas:
- If you only write to ALBEDO or EMISSION but forget DENSITY, the shader produces NO output
- Fog shader runs once per FROXEL (volumetric voxel) touched by the FogVolume's AABB — not per pixel
- A FogVolume that barely overlaps a froxel still runs the shader for that entire froxel
- Detail degrades with distance from camera (froxels get larger farther away)


<!-- keywords: Label3D, CANNOT, Cast, Shadow, Alpha, Cut, 3D -->
### Label3D CANNOT cast shadows regardless of Cast Shadow setting — use Alpha Cut for shadows (loses smoothness)
Label3D generates transparent quads internally (same as Sprite3D). Shadow casting is blocked for transparent geometry:
- Cast Shadow = On has NO effect on Label3D
- Workaround: use 'Alpha Cut' transparency mode to enable shadows, but text edges become jagged
- 'Opaque Pre-Pass' mode enables smooth shadows but causes transparency sorting artifacts
- TextMesh (3D mesh from text) supports shadows normally but does NOT support bitmap fonts (.fnt) — only TTF/OTF/WOFF
- Fonts with self-intersecting outlines (common in Google Fonts) render incorrectly in TextMesh — use official font source


<!-- keywords: MSAA, light -->
### MSAA does NOT antialias alpha scissor edges or specular highlights — only geometric silhouettes
3D antialiasing gotchas:
- MSAA only increases coverage samples; fragment shaders run ONCE per pixel. Alpha scissor (1-bit transparency) is completely unaffected by MSAA — must enable alpha antialiasing per-material
- TAA ghosting gets worse at lower framerates (slower convergence = more persistent ghost trails)
- FSR2 at NATIVE resolution is SLOWER than standard TAA — only use FSR2 if you're also lowering resolution for upscaling
- SSAA 2x scale = 4x pixels (per-axis!); 2x SSAA at 4K renders at 8K internally and can exhaust VRAM


<!-- keywords: Audio, SILENT, Driver, Enable, Input, AudioStreamPlayer, audio, input, Project, Settings -->
### Audio input produces SILENT recordings if 'Audio > Driver > Enable Input' project setting is not enabled
Recording from microphone silently fails (no error, no exception) if:
- Project Settings > Audio > Driver > Enable Input is not checked
- On iOS: Audio > General > iOS > Session Category must include 'Record' or 'Play and Record'
- The AudioStreamMicrophone resource must be assigned and the AudioStreamPlayer must be 'playing' before calling get_stream_playback()


<!-- keywords: Custom, Project, Settings, REPLACES -->
### Custom TLS certificate bundle in Project Settings REPLACES the OS bundle entirely — does not supplement it
When you set a custom certificate bundle in Project Settings > Network > TLS:
- The operating system's certificate bundle is completely replaced, not supplemented
- All certificate validation uses ONLY your custom bundle
- NEVER include the private key in the client-side certificate file — this silently compromises all TLS security
- HTTPClient on Web platform does NOT support blocking/synchronous requests — must use async polling pattern


<!-- keywords: SDFGI, Forward, Bounce, Feedback, rendering -->
### SDFGI only works in Forward+ renderer; Bounce Feedback above 0.5 causes infinite brightness feedback loop
SDFGI (Signed Distance Field Global Illumination) gotchas:
- Only available in Forward+ renderer — silent failure in Mobile or Compatibility renderers
- Bounce Feedback above 0.5 triggers exponential brightness feedback, making the scene blindingly bright within seconds. No warning.
- Modifying a Static GI mesh at runtime does NOT update SDFGI until the camera moves away and back into range
- GI takes 25 frames to converge after scene loads — causes visible flickering (no automatic fade)
- Fast camera movement degrades SDFGI quality significantly (cascade shift artifacts)
- Transparent materials use rough reflections regardless of their actual roughness value


<!-- keywords: Default, Latin -->
### i18n: Default font silently fails on non-Latin characters; CJK line breaking requires opt-in ICU data (+4MB)
Internationalization gotchas:
- The default Godot font only supports Latin-1. CJK (Chinese/Japanese/Korean), Arabic, Cyrillic, etc. render as blank squares with no error
- For languages without spaces (CJK, Thai), line/word breaking requires ICU data that is NOT included in exports by default. Enable 'Include Text Server Data' in export settings (+4MB)
- If a Label's text accidentally matches a translation key, it WILL be auto-translated. Disable Auto Translate Mode on labels displaying player names or dynamic strings
- For RTL (Arabic/Hebrew): layout and text direction are mirrored, but the X/Y coordinate system is NOT. Sprites/non-Control nodes are unaffected by RTL mirroring
- Test Locale project setting persists and shows up in version control — reset to empty before committing


<!-- keywords: Export, export -->
### Export path in --export-release CLI is relative to project.godot directory, NOT current working directory
Command line gotchas:
- `godot --export-release preset_name ./build/game.exe`: the `./build/` path is relative to the folder containing `project.godot`, not your shell's CWD
- Unknown/invalid command line arguments are SILENTLY IGNORED — no warning, no error
- On macOS: must run `Godot.app/Contents/MacOS/Godot`, not just `Godot` (it's a bundle/directory)
- `--recovery-mode` silently disables tool scripts, editor plugins, and GDExtension addons


<!-- keywords: Runtime, GLTFState, Texture, texture, GLTF -->
### Runtime glTF loading from buffer requires manually setting GLTFState.base_path or textures won't load
Runtime file loading gotchas:
- When loading glTF from bytes (append_from_buffer()), set `gltf_state.base_path` manually to the folder containing textures. From file, it's automatic; from buffer, it's not.
- Not all .ogg files are Ogg Vorbis — some are Ogg Theora (video) or Opus. AudioStreamOggVorbis.load_from_file() will fail silently on these
- VideoStreamPlayer: setting stream to null then back to a video breaks autoplay — must call play() explicitly even if autoplay is enabled
- ZIPReader.get_files() returns files in UNSORTED order — sort explicitly if processing order matters


<!-- keywords: SurfaceTool, MUST, TRIANGLES -->
### SurfaceTool: attributes MUST come immediately before add_vertex(); generate_normals() only works with TRIANGLES
SurfaceTool gotchas:
- All vertex attributes (set_normal, set_uv, set_color, etc.) must be called IMMEDIATELY before add_vertex(). Attributes set then overwritten before add_vertex() use the LAST value only. Attributes after the final add_vertex() are silently discarded.
- generate_normals() only works with PRIMITIVE_TRIANGLES — silently fails or produces garbage with other primitive types
- Normal mapping requires BOTH generate_normals() AND generate_tangents(). generate_tangents() requires UVs and normals to be set on ALL vertices first — if any vertex is missing UVs, tangent generation produces wrong results
- deindex() permanently expands the vertex array (removes index buffer), irreversibly increasing memory and draw calls


<!-- keywords: GPUParticles3D, DISABLES, particles -->
### GPUParticles3D sub-emitter assignment DISABLES emission on that particle system — cannot re-enable while assigned
Particle sub-emitter gotchas:
- When a GPUParticles3D node is assigned as a sub-emitter, it automatically stops emitting and the Emitting property cannot be re-enabled while assigned as sub-emitter. Silently produces nothing if you test the sub-emitter node in isolation.
- A particle system cannot be its own sub-emitter — produces nothing silently (no error)
- VectorField attractor texture: neutral gray (0.5, 0.5, 0.5) means ZERO velocity (not default direction). Y axis is inverted between texture UV space and world space — top of texture = bottom of attractor volume


<!-- keywords: Ragdoll, collision, physics, Bone -->
### Ragdoll: collision layers must differ from character capsule or bones collide with each other on activation
Ragdoll system gotchas:
- Physical bones and the character's capsule CollisionShape must be on DIFFERENT collision layers, or the ragdoll bones collide with the capsule and bounce uncontrollably when simulation starts
- physical_bones_start_simulation() with nonexistent bone names produces NO error or warning — bones simply don't simulate, leaving the character motionless
- Default PinJoint type causes visual 'crumpling' of characters — change to HingeJoint3D or ConeTwistJoint3D for anatomically correct ragdolls
- Adjusting a joint's rotation ALSO rotates its collision shape — adjust joints before collision shapes to avoid double-work


<!-- keywords: Compositor, RENDERING, THREAD, rendering, Thread, Mutex, threading -->
### Compositor effects run on the RENDERING THREAD — accessing game state requires mutex; push constants need 16-byte alignment
Compositor (custom render effects) gotchas:
- Compositor effect code runs on the rendering thread. Accessing any game state (node positions, game variables) requires mutex protection to avoid crashes
- Push constants MUST be padded to 16-byte alignment (array length must be a multiple of 4 floats). Missing padding causes shader compilation or runtime errors
- Compositor effects only have access to the 3D rendering pipeline — 2D post-processing is NOT supported
- free_rid() is POSTPONED until all in-flight frames finish — freed resources aren't immediately deallocated


<!-- keywords: Physics, collision, physics, _physics_process, int, 3D -->
### Physics interpolation: transforms changed outside _physics_process() cause jitter; 3D collision shapes don't interpolate
Physics interpolation additional gotchas:
- Setting object transforms (or their parent transforms) outside _physics_process() makes interpolation incorrect — may only appear on some players' hardware depending on frame timing
- In 3D: visible collision shape outlines do NOT reflect physics interpolation, appearing slightly ahead of the visual mesh. This is a known limitation (2D correctly shows interpolation)
- Order for initial placement: set transform → reset_physics_interpolation() → then set the first physics-tick transform. Wrong order causes streaking from origin


<!-- keywords: All, GDScript, MUST, gdscript, @tool, EditorPlugin -->
### All GDScript files in an editor plugin MUST have @tool — without it they silently act as empty files
Editor plugin gotchas:
- Every GDScript file your plugin uses (not just EditorPlugin.gd) must have the `@tool` annotation at the top. Without it, the script is silently ignored in the editor context — no error, no warning
- Main screen plugins: call `make_visible(false)` immediately after adding the panel in `_enter_tree()` — skipping this makes the panel permanently visible and competing for space
- C# plugins must be built (compiled) before enabling them in Project Settings — enabling an unbuilt plugin silently fails


<!-- keywords: Sync, XR -->
### XR: V-Sync must be disabled or framerate is capped at monitor refresh; initialize() must NOT be called multiple times
XR setup gotchas:
- V-Sync enabled at 60Hz caps OpenXR output to 60 FPS instead of the required 90+ FPS. Disable V-Sync for all XR projects
- Physics runs at 60 Hz by default; XR often runs at 90 Hz. Set `Engine.physics_ticks_per_second` to match XR framerate or physics feels choppy
- Calling initialize() multiple times across scene changes has 'unpredictable effects' — load levels as sub-scenes instead of calling initialize() in each scene
- A registered XR interface does NOT mean it's initialized successfully. Always check is_initialized() explicitly — registration only means the interface is available


<!-- keywords: EVERY, FRAME, Filmic, ACES -->
### HDR output: output_max_linear_value changes EVERY FRAME — never cache it; Filmic/ACES tonemappers ignore HDR entirely
HDR output gotchas:
- output_max_linear_value changes every frame as brightness/HDR settings change. Retrieve it each frame; caching will break HDR scaling
- Filmic and ACES tonemappers completely IGNORE output_max_linear_value and always produce SDR-range output — do not use them for HDR
- Mixing WorldEnvironment glow with output_max_linear_value causes glow strength to increase with higher max values (undesirable)
- HDR screens may do their own dynamic tonemapping (DTM) that dims the image when bright pixels exceed 1-10% of screen — unpredictable across devices


<!-- keywords: GPUParticles3D, Visibility, AABB, collision, 3D, particles -->
### GPUParticles3D collision: SDF must be manually baked; Visibility AABB must overlap collider or no collision occurs
3D particle collision gotchas:
- SDF collision must be MANUALLY baked in the editor — if you forget to bake, particles pass through everything silently. Also: SDF collision is Forward+/Mobile only (not Compatibility)
- The particle's Visibility AABB must overlap the collider's AABB for collision to work at all. If particles spawn outside the AABB, they won't collide. Use GPUParticles3D > Generate Visibility AABB
- Height field 'When Moved' update mode only triggers when the HEIGHT FIELD NODE itself moves — NOT when meshes inside it move. Dynamic geometry is not supported
- Low resolution height fields can silently IGNORE small meshes entirely (not just fuzz them — they disappear from collision)
- Fast particles tunnel through thin colliders — increase collider thickness or Fixed FPS (but higher FPS has significant performance cost)


<!-- keywords: Godot, SceneMultiplayer, TileMap, Android, scene, Node, tile -->
### Godot 4.3 breaking changes: SceneMultiplayer protocol incompatible with 4.2; TileMap layers became separate nodes; Android auto-permissions removed
Upgrading to 4.3 gotchas:
- SceneMultiplayer: caching changed to send ID instead of node path — 4.2 and 4.3 clients CANNOT communicate with each other, no graceful degradation
- TileMap layers are now individual TileMapLayer nodes — complex tilemap setups require significant refactoring
- Android: permissions are NO LONGER requested automatically — must call request_permission() explicitly or apps silently fail
- Reverse-Z depth buffer was introduced in 4.3 — custom depth-reading shaders break without manual updates (POSITION must use 1.0 for near, not 0.0)
- auto_translate inheritance changed: parent with auto_translate=false now cascades to children, possibly silently disabling translations


<!-- keywords: Particle, Forward, Mobile, Transform, Align, Velocity, velocity -->
### Particle trails only work in Forward+/Mobile; tube trails flatten without 'Transform Align = Y to Velocity'
3D particle trail gotchas:
- Particle trails are Forward+/Mobile only — silent failure in Compatibility renderer
- Ribbon trails using tubes flatten and lose volume during direction changes unless 'Transform Align' is set to 'Y to Velocity'
- Turbulence modifies existing movement — it does NOT create movement. Zero-gravity particles with only turbulence enabled show nothing
- Turbulence Noise Speed moves the NOISE PATTERN (not particles). Confusing it with particle speed produces hard-to-debug results
- 3D turbulence noise is very expensive on mobile/web — causes dramatic frame drops on multiple emitters


<!-- keywords: Godot, Upgrade, Only, Mesh, mesh -->
### Godot 4.2: mesh 'Upgrade Only' causes permanent per-launch upgrade cost; mesh format is one-way (can't open in 4.0/4.1)
Godot 4.2 upgrade gotchas:
- When asked to upgrade meshes, choosing 'Upgrade Only' (not 'Restart & Upgrade') upgrades in-memory ONLY — every project launch re-triggers the upgrade, permanently slowing load times
- The new 4.2 mesh format cannot be opened in Godot 4.0 or 4.1 — one-way upgrade
- AnimationTree.tree_root type changed from AnimationNode to AnimationRootNode — breaks C# source and binary compat with no compatibility shim
- GraphNode properties (comment, overlay, language, show_close) removed with no replacement — C# code using these won't compile


<!-- keywords: Bool, Float, Input, input, float, XR -->
### XR: Bool type from analog inputs unreliable — use Float with your own threshold; user binding overrides are allowed by spec
XR action map gotchas:
- XR runtimes do NOT guarantee sensible threshold for analog→Bool conversion. A trigger at 10% may register as pressed. Use Float type and apply your own threshold.
- XR runtimes are ALLOWED to let users customize/override your action bindings — your action map is a suggestion, not a guarantee
- No reliable way to know which physical controller the user has — you only know the profile, not the hardware
- Touch vs Click distinction (some controllers support both 'X touch' and 'X click') is not universal — relying on it breaks on unsupported devices


<!-- keywords: Code, Godot, Windows, instantiate -->
### VS Code for Godot on Windows must use 'code.cmd' not 'code.exe'; LSP/DAP require a running Godot instance
External editor gotchas:
- On Windows, setting VS Code path to 'code.exe' fails — must use 'code.cmd' (the batch file wrapper)
- LSP (port 6005) and DAP (port 6006) require Godot to be RUNNING. Closing the editor breaks all language server features in your external editor
- Visual Studio cannot jump to specific lines (no {line} support) — only opens the file


<!-- keywords: ParticleProcessMaterial, MULTIPLY, Angle, Rotate, Color, Material, material -->
### ParticleProcessMaterial: curves MULTIPLY base value (don't replace); Angle requires Rotate Y enabled; Color multiplies material color
GPU particle process material gotchas:
- If a Curve is set on any property (scale, velocity, etc.), it MULTIPLIES the Min/Max base value — it does not replace it. A curve returning 1.0 leaves the base unchanged; 0.0 zeroes it.
- The Angle property has ZERO visible effect unless 'Rotate Y' is enabled OR the material uses 'Particle Billboard' mode
- Color property MULTIPLIES with the material's existing Color/Texture — white (1,1,1,1) leaves material color unchanged; to override, set material texture to white
- Tangential acceleration is permanently restricted to the XZ-plane — particles never move along Y due to tangential force
- Damping only affects linear velocity — does NOT slow particle rotation (angular velocity)
- Direction property alone has ZERO effect without velocity or acceleration set
- Animation smoothness depends on: Animation FPS = (Number of frames) / Lifetime. Changing Lifetime without adjusting animation speed breaks frame timing


<!-- keywords: EXIT, int, Thread, threading, print, debug -->
### print() stdout only flushes on EXIT in release builds — use printerr() for real-time logging; custom loggers must be thread-safe
Logging gotchas:
- In RELEASE builds, print() output is only written to the log file when stdout is flushed at exit/crash. Debug builds flush every print. This means print() is useless for real-time diagnostics in shipped games.
- printerr(), push_error(), push_warning() flush IMMEDIATELY in both debug and release — use these for critical runtime logging
- Script backtraces are DISABLED in release builds for performance. Enable 'Debug > Always Track Call Stacks' to get actionable error traces in production
- Custom logger functions are called from ANY thread (including non-main threads) and possibly concurrently — must be thread-safe
- Using print()/push_error() INSIDE a custom logger causes silent suppression (infinite recursion prevention) instead of your expected output


<!-- keywords: IMMEDIATE, queue_free, Node -->
### free() is IMMEDIATE and cascades to ALL children; prefer queue_free() — dangling references crash instantly
Node deletion gotchas:
- free() destroys the node AND all its children IMMEDIATELY. Any code holding a reference to the node or its descendants will crash on next access.
- queue_free() defers deletion to end of frame — safe for nodes that may still be referenced in current frame logic
- NEVER call free() on a node that may receive more signals or callbacks in the same frame
- get_node() on a node not yet added to the scene tree fails silently — always access nodes in _ready() or later, not in _init()


<!-- keywords: MeshDataTool, Mesh, mesh -->
### MeshDataTool: only works with PRIMITIVE_TRIANGLES; create_from_surface() silently clears existing data; must call commit_to_surface()
MeshDataTool gotchas:
- Only supports PRIMITIVE_TRIANGLES — silently fails or produces garbage with line/point/strip primitives
- create_from_surface() silently clears ALL existing data in the MeshDataTool instance without warning — data loss if called on initialized tool
- Changes to vertex data are NOT applied to the mesh until commit_to_surface() is explicitly called
- Manually accumulating face normals for vertex normals does NOT produce mathematically perfect smooth normals (only approximates)
- Significantly slower than direct ArrayMesh manipulation — use ArrayMesh when performance matters


<!-- keywords: CRASH, queue_free, @tool -->
### @tool scripts: queue_free() can CRASH the editor; every dependency must also be @tool; restart required after adding @tool
@tool script gotchas:
- Calling queue_free() or free() in a @tool script runs in the editor and CAN CRASH the editor — there is no protection
- Every GDScript instantiated by a @tool script MUST also have @tool. Non-@tool scripts cannot be instantiated in tool mode (silently acts as empty)
- Exception: static methods, constants, and enums from non-@tool scripts CAN be accessed from @tool scripts without @tool
- Known Godot 4 bug (GH-66381): adding @tool to an existing script requires an EDITOR RESTART to take effect
- Modifying a @export Resource property's sub-properties in inspector does NOT trigger the property's setter — setter only fires when creating/deleting/pasting the resource itself


<!-- keywords: ProjectSettings, EFFECT, settings -->
### ProjectSettings.set_setting() at runtime has NO EFFECT on startup-read settings — use runtime class properties instead
Many project settings are read ONCE at engine startup and cached in runtime classes. Calling ProjectSettings.set_setting() afterwards does nothing:
- Physics settings → PhysicsServer2D/3D
- Rendering settings → RenderingServer
- Window/display settings → Window or Viewport
- Engine settings → Engine class
Always modify the runtime class directly (e.g., Engine.physics_ticks_per_second) instead of ProjectSettings at runtime.


<!-- keywords: LightmapGI, MUST, scene, Node, light, Control, import, resource -->
### LightmapGI: max 8 simultaneous nodes; UV2 lost on scene reimport; .unwrap_cache MUST be in version control
LightmapGI gotchas:
- Only 8 LightmapGI nodes can render simultaneously — having more causes flickering/popping with no warning
- LightmapGI only bakes SIBLINGS and DIRECT CHILDREN — not deeper descendants unless they're explicitly within scope
- UV2 generated via 'Mesh > Unwrap UV2' is LOST when the source scene is reimported. Store as separate resource.
- `.unwrap_cache` files MUST be committed to version control — ignoring them causes inconsistent UV2 results across machines/versions
- Lightmap baking is NOT supported in the web editor (graphics API limitation)


<!-- keywords: VoxelGI, Forward, ShaderMaterial, BLACK, Node, material, mesh, shader -->
### VoxelGI: Forward+ only; max 8 nodes, max 2 overlap; custom ShaderMaterial meshes bake as pure BLACK
VoxelGI gotchas:
- VoxelGI only works in Forward+ renderer — silent failure in Mobile or Compatibility
- Maximum 8 VoxelGI nodes render simultaneously (more = flickering)
- Only 2 VoxelGI nodes can BLEND at a single pixel — more than 2 overlapping causes flickering as camera moves
- Meshes using ShaderMaterial are treated as PURE BLACK during baking (only block light, emit nothing) — only StandardMaterial3D/ORMMaterial3D contribute color to baked GI
- BaseMaterial3D UV1 offset/scale, triplanar, and Emission > On UV2 properties are all IGNORED during VoxelGI baking


<!-- keywords: RPCs, BOTH, Node, add_child, RPC -->
### RPC: ALL RPCs must exist on BOTH peers; dynamic add_child() nodes need force_readable_name=true or RPCs silently fail
High-level multiplayer RPC gotchas:
- RPC signatures are verified by a CHECKSUM of ALL RPCs in the script. If any peer has a different set of RPCs (even unused ones), ALL RPCs fail for that script — no partial matching
- Dynamic nodes added via add_child() get auto-generated names (e.g., '@@2'). These names differ between peers, causing NodePath mismatch and silently failing RPCs. Fix: use `add_child(node, true)` (force_readable_name=true)
- High-level multiplayer uses UDP ONLY — port forwarding for WAN hosting must be UDP, not TCP
- RPC function argument count is NOT validated during checksum — `func foo()` and `func foo(a, b)` match each other, causing runtime mismatch without checksum failure


<!-- keywords: GridMap, MeshInstance3D, IGNORED, Node, Material, material, Mesh, mesh -->
### GridMap: materials on MeshInstance3D node are IGNORED — only materials embedded in the mesh are used
GridMap gotchas:
- When generating a MeshLibrary, materials assigned to the MeshInstance3D NODE (not embedded in the mesh resource) are silently ignored. Must embed materials in the mesh itself.
- Lightmap texel size must be set in Import dock BEFORE converting to MeshLibrary — changing it afterwards has no effect on existing library data
- NavigationMesh baked cell size must EXACTLY match the NavigationServer map cell size for proper merging — silent failure if mismatched
- Only MeshInstance3D, StaticBody3D, CollisionShape3D, NavigationRegion3D nodes are exported — all other node types are silently ignored


<!-- keywords: Web, IndexedDB, LOST -->
### Web editor: project data in IndexedDB is LOST if browser storage is cleared; files outside /home/web_user/ lost on close
Web editor limitations:
- Project files are stored in browser IndexedDB (not real filesystem) — clearing browser storage or site data destroys the project with NO warning
- Files/projects placed outside `/home/web_user/` are LOST when closing the browser tab
- No auto-save warning when closing the tab with unsaved changes (unlike native editor)
- Cannot open new windows — running the game replaces the editor view
- Lightmap baking is unavailable (import only); GDExtension plugins are unsupported


<!-- keywords: EditorScript, SILENTLY, LOST, scene, instantiate, Node, @tool, editor -->
### @tool EditorScript: changes to nodes in instanced subscenes are SILENTLY LOST on scene reload
@tool EditorScript gotchas:
- Modifications made by EditorScript to nodes that belong to instanced subscenes (not the root scene) are silently discarded when the scene reloads
- EditorScript has NO undo/redo — all changes are permanent. Save the scene before running.
- C# EditorScripts cannot be run from the Script Editor toolbar — only via command palette or file system context menu


<!-- keywords: AnimationTree, SHARED, RESET, scene, instantiate, Node, Resource, animation -->
### AnimationTree: node resources SHARED across all scene instances; RESET animation must have exactly 1 frame
AnimationTree gotchas:
- AnimationTree node resources (BlendSpace, StateMachine, etc.) are SHARED between ALL instances of a scene. Modifying blend values on one instance affects ALL instances. Use unique resources per instance.
- RESET animation is NOT for playback — it defines the default pose baseline for blending. It must have exactly ONE frame at t=0. Playing it directly produces wrong results.
- Rotation blending with Linear/Cubic interpolation ALWAYS takes the shortest path through the bone's rest pose — rotations > 180° from rest are impossible in blended animations. Import T-pose skeletons to maximize interpolation range.
- StateMachine requires an explicit `start()` call OR connection to the 'Start' node before `travel()` works
- When using Polygon2D with bones, newly added INTERNAL vertices must also have weights painted — omitting weight painting causes unintended deformation


<!-- keywords: Viewport, EMPTY, SubViewport, rendering, 3D -->
### Viewport.get_texture().get_image() returns EMPTY on first frame; 3D shadows need positional_shadow_atlas_size > 0 (default 0)
Viewport gotchas:
- Viewport has no get_image(): use get_texture().get_image(). On the first frame that image is EMPTY. Must await RenderingServer.frame_post_draw signal before reading viewport texture.
- New Viewports have positional_shadow_atlas_size = 0 by default — 3D point/spot light shadows are completely invisible until this is set > 0
- Viewport texture is CLEARED every frame by default. For persistent drawing (e.g., paint canvas), set viewport.render_target_clear_mode = CLEAR_MODE_NEVER
- SubViewport does NOT receive mouse or touch input unless it has a SubViewportContainer as parent
- SubViewportContainer cannot be smaller than its child SubViewport's size (hard constraint)
- Only ONE camera per Viewport can be active — multiple cameras with current=true will conflict; use make_current() to switch


<!-- keywords: Compatibility, Sync, Shader, shader, rendering, FPS, delta -->
### Compatibility renderer: pre-spawn all shaders at level load to avoid runtime compilation stutter; V-Sync halves FPS on miss
Rendering/jitter gotchas:
- Compatibility renderer (OpenGL) does NOT use Godot's ubershader approach — shaders compile ON DEMAND the first time they're needed, causing frame stutter. Workaround: show a loading screen and force-render everything once before gameplay
- Double-buffered V-Sync drops to HALF refresh rate (e.g., 60Hz → 30 FPS) if you miss a frame. Use triple-buffering or adaptive V-Sync if you can't consistently hit the target
- Physics interpolation adds INPUT LAG for physics-controlled objects (player movement). Only enable if visual jitter is worse than acceptable lag
- Different physics_ticks_per_second values produce DIFFERENT simulation outcomes even with consistent delta — let players change this in competitive games and they gain unfair physics advantages


<!-- keywords: Godot -->
### gettext/PO: 'fuzzy' comment after msgmerge silently prevents translation from loading in Godot
Localization with gettext gotchas:
- After running `msgmerge`, strings marked with `#, fuzzy` comment are SILENTLY IGNORED by Godot — they look correct in the file but never appear in-game. Must manually remove the fuzzy comment.
- `msgmerge` parameter order is critical: `msgmerge [po-file] [pot-template]` — reversing them corrupts the translation file
- `.pot` template files must have EMPTY `msgstr` values — translations belong only in `.po` files. Non-empty msgstr in .pot causes incorrect base strings.
- The Godot gettext extractor only finds localizable strings in CONSTANT string expressions — dynamic strings and concatenations are silently skipped


<!-- keywords: Call, Method, Track, DISABLED, Bezier, Property, property -->
### Call Method Track events are DISABLED during editor preview; Bezier and Property tracks cannot be blended together
Animation track gotchas:
- Call Method Track events are intentionally DISABLED when previewing animations in the editor (for safety). They only execute in-game. Test method call effects by running the game, not scrubbing the timeline.
- Bezier curve tracks and regular Property tracks CANNOT be blended in AnimationPlayer or AnimationTree — pick one type per property and stick with it
- AnimationPlayer CANNOT reference itself in an Animation Playback Track — only external AnimationPlayers
- Instanced scene AnimationPlayer is inaccessible until 'Editable Children' is enabled in the scene tree
- IK chains set up in the editor are EDITOR-ONLY — they do NOT function at runtime for procedural animation


<!-- keywords: Apply, Root, Scale, scene, Node, Mesh, mesh, int, Node3D, 3D, import, resource -->
### 3D import: 'Apply Root Scale' bakes scale into mesh data permanently; inherited scenes cannot delete base nodes
3D scene import gotchas:
- 'Apply Root Scale' bakes the root transform scale INTO the meshes and animations, not the root Node3D. Nodes added as children after import won't automatically inherit this scale.
- When using scene inheritance on an imported .glb/.fbx scene, you CANNOT delete or rearrange nodes from the base scene — only add new nodes
- Subresources (materials, meshes) in inherited scenes are READ-ONLY. To edit them, save the subresource as a separate .tres/.res file first
- If 'Limit Playback' is enabled, animations are clipped to Blender's timeline playback range — missing keyframes outside that range


<!-- keywords: Importing, Godot, Mipmaps, Limit, Texture, texture, rendering, 3D, import -->
### Importing images: SVG text silently doesn't render; Godot auto-reimports 3D textures with mipmaps; 'Mipmaps Limit' is broken
Image import gotchas:
- SVG text is NOT rasterized by Godot's ThorVG library — text in SVG files simply doesn't appear. Convert text to paths in your SVG editor first.
- When Godot detects a texture is used in 3D, it SILENTLY auto-reimports it with mipmaps enabled and changes compression. This is automatic and may surprise you.
- The 'Mipmaps > Limit' setting in the import dock is NOT implemented — it exists in the UI but has no effect
- PNG images are always imported at 8 bits per channel — no HDR support. Use EXR or HDR format for high-precision textures.
- Pixel art textures should disable VRAM compression to avoid noticeable artifacts, even in 3D (performance cost is worth it for pixel art)


<!-- keywords: GDScript, MUST, gdscript -->
### GDScript custom iterators: _iter_init() MUST reset state or nested loops using the same iterator silently fail
GDScript advanced gotchas:
- Custom iterators must completely reset their state in _iter_init(). Failure to reset means the second for-loop over the same object continues from where the first left off — nested loops break silently.
- Only primitive types (int, float, String, Vector2, etc.) are passed BY VALUE to functions. Everything else (Object instances, Arrays, Dicts) is passed BY REFERENCE.
- Classes inheriting RefCounted are auto-freed when unreferenced; classes inheriting Object directly require manual free() calls


<!-- keywords: Looping, NEVER, Ogg, signal, emit, AudioStreamPlayer, audio -->
### Looping audio NEVER emits 'finished' signal; Ogg/MP3 only support forward loops (no ping-pong/reverse)
Audio import gotchas:
- AudioStreamPlayer.finished signal NEVER fires for audio streams with Loop enabled — any cleanup or chaining logic tied to 'finished' will never run for looping audio
- Ogg Vorbis and MP3 only support a single loop begin point with forward looping. WAV format supports ping-pong and backward loop modes; Ogg/MP3 do not.
- 24-bit audio provides zero perceptible quality benefit in games — wastes memory and load time. 16-bit is sufficient.
- The 'Trim' import option removes audio below -50 dB (not absolute silence) — very quiet content at the start/end is also trimmed, potentially adding unexpected latency


<!-- keywords: Video, WebM, URLs, FFmpeg, Windows, Theora -->
### Video: CPU-only decoding; WebM removed in 4.0; streaming URLs not supported; FFmpeg 64-bit on Windows corrupts Theora
VideoStreamPlayer gotchas:
- Video decoding is ENTIRELY CPU-based — no GPU acceleration. High resolution (1440p+) videos will stutter on mobile and low-end hardware
- WebM video format was REMOVED in Godot 4.0 — Ogg Theora (.ogv) is the only built-in video codec
- Streaming video from URLs is NOT supported — only local files work
- Multichannel audio (5.1, 7.1) in videos is automatically downmixed to stereo with no option to preserve
- CRITICAL: 64-bit FFmpeg on Windows produces CORRUPTED Theora output — must use 32-bit FFmpeg to encode .ogv files
- Setting Video Delay Compensation to non-zero causes frame drops at loop boundaries


<!-- keywords: MUST, scene, 3D, import -->
### 3D import: _post_import() MUST return the scene or all modifications are silently discarded
When using an import script with _post_import(scene):
- The function MUST `return scene` (or the modified scene) at the end. Failing to return it silently discards ALL changes made during post-import processing — the original unmodified scene is used instead.
- Track filter rules in import are evaluated IN ORDER — an excluded track can be re-included by a later rule, making filter results order-dependent
- 'Ensure Tangents' is required for normal maps to display correctly — if disabled and the model has no tangent data, normal maps render wrong with no error


<!-- keywords: WEAKER, DirectionalLight2D, light, 2D -->
### 2D lights: background color is unaffected by lights; normal maps make lights appear WEAKER; DirectionalLight2D shadows are infinitely long
2D lighting gotchas:
- The canvas background color (Project Settings > Rendering > Background) is NEVER affected by 2D lights. Must place a Sprite2D/ColorRect to represent the background for it to receive lighting.
- Adding normal maps to sprites makes lights appear significantly WEAKER — must increase light Height and Energy properties to compensate
- PointLight2D.offset moves the glow texture but NOT the shadow origin — shadows stay anchored to the node position regardless
- DirectionalLight2D shadows are INFINITELY LONG by default and cannot be made finite through height adjustment — requires custom SDF shaders
- PCF13 soft shadows are very expensive — only use for a handful of lights. Multiple PCF13 lights cause severe frame drops.
- Filter Smooth values above ~4.0 with PCF5 create visible streaking artifacts instead of smooth shadows


<!-- keywords: Container, OVERRIDES, MarginContainer, Theme, Control -->
### Container silently OVERRIDES child position/size; MarginContainer needs theme constant overrides to have any margin
GUI Container gotchas:
- Any manual position or size set on Control nodes INSIDE a Container is silently overridden/reset whenever the Container resizes. The Container takes complete layout control — do NOT manually position children inside containers.
- MarginContainer only applies margins if the theme's constant overrides define them. Without theme overrides, MarginContainer behaves identically to a regular container with zero margins.
- Fill and Expand flags are independent: a control can Expand (takes extra space in layout) without Fill (using that space), leading to surprising gaps. Fill defaults ON, masking this behavior.
- Control anchors use anchor-relative offsets: when anchor is at 1.0 (right/bottom), NEGATIVE offset values move the control left/up — opposite of what feels intuitive.


<!-- keywords: MUST, Mesh, mesh, export, 3D, Bone, import -->
### 3D model: skeleton MUST be in rest pose before export or mesh imports permanently deformed; look_at() needs use_model_front
3D model export gotchas:
- If the skeleton is NOT reset to rest/T-pose before export, Godot imports the mesh with permanently deformed geometry matching whatever pose the bones were in at export time
- N-gon/quad auto-triangulation during import produces UNPREDICTABLE geometry. Always triangulate in your 3D software before export.
- look_at() uses a convention where model front is -Z (opposite of 3D software convention). Call `look_at(target, Vector3.UP, true)` (use_model_front=true) for assets designed to face +Z.
- Imported lights render very differently than in the source 3D software — design lighting in Godot rather than importing it


<!-- keywords: ALWAYS, Node, 3D, import, global -->
### 3D import naming: -noimp suffix ALWAYS removes the node even if name suffix processing is globally disabled
Import node name customization gotchas:
- The `-noimp` suffix (removes node during import) is respected EVEN WHEN all name suffix processing is globally disabled in import settings — it's the only suffix that cannot be opted out of
- Convex collision shapes on concave meshes create WRONG geometry: a hollow box becomes a solid box. Only use convex shapes on actually convex meshes.
- Animation `loop`/`cycle` suffixes do NOT require a `-` prefix (unlike all other suffixes) — naming inconsistency that's easy to miss


<!-- keywords: Web, _process, Input, input, export -->
### Web export: background tab silently stops _process(); fullscreen/cursor capture requires an input event callback
Web/HTML5 export gotchas:
- When the user switches to another browser tab, `_process()` and `_physics_process()` STOP RUNNING silently. Networked games will disconnect without realizing why.
- Fullscreen mode and cursor capture (`Input.mouse_mode = Input.MOUSE_MODE_CAPTURED`) CANNOT be called from regular code — must be called from within an active input event callback (_input, _unhandled_input). Calling from _ready() or a button's pressed signal does NOT work.
- Audio autoplay is BLOCKED by browsers — user must interact (click/tap/key) before any audio plays. Display a splash/start screen.
- Gamepads are not detected until the user presses a button (browser privacy limitation)
- Service worker caching shows OLD project version — must manually unregister service worker in browser DevTools after deploying an update
- Threaded web exports require CORS headers: `Cross-Origin-Opener-Policy: same-origin` AND `Cross-Origin-Embedder-Policy: require-corp`. Missing these causes threaded export to silently fail.
- .wasm file must be served with `application/wasm` MIME type or startup optimizations are bypassed


<!-- keywords: Feature, IGNORES -->
### Feature tags: get_setting() IGNORES overrides; 'mobile' returns false on web; custom tags don't work in editor
Feature tag gotchas:
- `ProjectSettings.get_setting('key')` completely IGNORES feature tag overrides. Must use `ProjectSettings.get_setting_with_override('key')` to respect overrides.
- `OS.has_feature('mobile')` returns FALSE when a web export runs on a mobile device. Use `OS.has_feature('web_android')` or `OS.has_feature('web_ios')` instead.
- Custom feature tags defined in export presets are SILENTLY IGNORED when running from the editor — they only apply to exported builds
- Feature tag names are CASE-SENSITIVE: 'debug' works, 'Debug' does not


<!-- keywords: GDScript, Array, gdscript, typed, int -->
### GDScript 'as' keyword silently returns null on type mismatch; nested typed arrays (Array[Array[int]]) not supported
GDScript static typing gotchas:
- The `as` keyword returns NULL (no error, no warning) if the cast fails. Code like `var x: MyClass = node as MyClass` silently gives null if node is wrong type, causing NullReferenceError later.
- Nested typed collections are NOT supported: `Array[Array[int]]` and `Dictionary[String, Dictionary[String, int]]` are syntax errors. Must use untyped nesting.
- Enum type hints are just `int` at runtime — no validation that the value is a valid enum member. Must validate manually.
- Typed Array/Dictionary methods like push_back() and equality == are UNTYPED internally — type violations can slip through silently


<!-- keywords: BEFORE, Node -->
### _draw() is cached — call queue_redraw() to update; draw_set_transform() applies BEFORE node transform
Custom 2D drawing gotchas:
- `_draw()` is called once and the result is CACHED. Changing any variable that affects drawing has NO visual effect until you call `queue_redraw()` explicitly.
- `draw_set_transform()` applies BEFORE the node's own transform — resulting in unintuitive pivot behavior. The custom transform is local, not applied on top of node position.
- Lines with ODD pixel widths must have coordinates shifted by 0.5 to center correctly (e.g., draw from (0.5, 0) to (0.5, 10) for a 1px vertical line at x=0)
- draw_line(), draw_polyline(), draw_multiline(), draw_arc(), draw_circle() and draw_rect() take an `antialiased` argument; draw_polygon() and the texture/string calls do not — for those, enable 2D MSAA in the project settings
- Draw call order is the ONLY depth control — later calls overdraw earlier ones; no z-ordering within a single _draw() invocation


<!-- keywords: Resource, load, preload -->
### PCK/ZIP files: loaded resources silently override existing same-path resources; preload() defeats dynamic pack loading
Resource pack (PCK) gotchas:
- When loading a PCK/ZIP file, resources with the SAME path as existing resources are silently replaced with no warning — can corrupt saves or game state if paths collide
- Packs loaded after `preload()` calls have already executed will NOT affect those preloaded resources. Loading a pack in _ready() doesn't patch preloaded menu assets.
- C# projects must manually call `Assembly.LoadFile()` to load the DLL before loading the resource pack — extra step not required in GDScript
- Dedicated server 'Strip Visuals' mode: placeholder textures store ONLY the image dimensions, not pixel data. Anything reading pixel colors (collision baking from image, procedural code) will silently get wrong data


<!-- keywords: PhysicsDirectSpaceState, LOCKED, physics, _process, _physics_process, RayCast -->
### PhysicsDirectSpaceState is LOCKED outside _physics_process() — only do raycasts/shape queries there
Physics raycasting gotchas:
- PhysicsDirectSpaceState (used for ray_intersect, shape_intersect, etc.) is LOCKED outside of `_physics_process()`. Calling it from `_input()`, `_process()`, or other callbacks causes errors.
- Must pass `collide_with_areas = true` in query params to detect Area2D/3D hits — by default, only physics bodies are hit
- Use `query.exclude = [get_rid()]` to prevent self-collision on character body raycasts
- Cylinder CollisionShape3D is UNSTABLE in physics simulation — use boxes or capsules instead
- Scaling a physics body (set_scale()) does NOT work — change collision shape Extents/Radius directly
- Stacked physics objects wobble at 60 physics ticks/second — increase to a multiple of 60 (120, 180, 240) for stability


<!-- keywords: Asset, ResourceLoader, FileAccess, Resource, load, export, file, import, resource -->
### Asset import: use ResourceLoader (not FileAccess) — FileAccess works in editor but fails in exported builds
Asset pipeline gotchas:
- FileAccess.open() works in the editor but FAILS for imported assets (images, audio, meshes, etc.) in exported builds — use ResourceLoader.load() instead
- ResourceLoader can ONLY access imported resources — it cannot read raw non-imported files (use FileAccess for those)
- `.import` metadata files MUST be committed to version control — without them, assets cannot be reimported correctly on other machines
- The `.godot/imported/` folder should NOT be committed — it's large and auto-regenerated from the .import files


<!-- keywords: Parallax, Texture, texture, 2D -->
### 2D Parallax: texture origin must be at (0,0), NOT centered; infinite repeat expands right/down only
2D Parallax gotchas:
- Parallax repeat tiles starting from (0,0), expanding RIGHT and DOWN. Textures placed with negative x/y or centered at viewport center will NOT tile correctly.
- If the parallax texture is too small for the viewport, either scale the ParallaxLayer node or tile the texture itself — small textures break at zoom levels
- Split-screen parallax requires CLONING the parallax nodes into each SubViewport with visibility layer masking — a single parallax setup cannot be shared across viewports


<!-- keywords: Godot, INCLUDES, English, Bone, animation -->
### Godot 4 bone pose INCLUDES bone rest (changed from Godot 3); auto-retargeting needs English bone names
3D skeleton/retargeting gotchas:
- Godot 4 changed how bone poses work: the pose transform now INCLUDES the bone rest. In Godot 3, pose was applied ON TOP of rest separately. Porting code that manipulates bone transforms directly requires this adjustment.
- Skeleton retargeting auto-mapping uses pattern matching on bone names — use standard English anatomical names (Hips, Spine, Head, UpperArm, etc.) for best auto-detection
- 'Unimportant Positions' option removes position tracks except root and scale_base — important for sharing animations across different-sized characters


<!-- keywords: Large, GDExtensions, GPUParticles, Shader, shader, GPUParticles3D, particles, GDExtension -->
### Large world coordinates: GDExtensions must be recompiled; triplanar/GPUParticles still jitter; fragment shader can't reconstruct world pos
Large world coordinates (double precision) gotchas:
- GDExtensions compiled for single-precision builds are INCOMPATIBLE with double-precision builds (and vice versa) — all extensions must be recompiled when switching. No error is thrown; things just break.
- Triplanar materials and GPUParticles3D with 'Local Coords' disabled still exhibit visible jitter far from origin even WITH large world coordinates enabled
- In double-precision builds, you CANNOT reconstruct world coordinates from view-space in a fragment shader using normal matrix math. Must calculate world coordinates in the vertex shader and pass them as varyings.
- Networked games with mismatched client/server precision (single vs. double) will appear to work but silently produce 'various issues' — all peers must use the same precision build
- 2D rendering does NOT benefit from double precision (only 2D physics does) — model snapping still occurs from a few million pixels at normal zoom
- Performance penalty is severe on 32-bit CPUs and mobile hardware — consider reducing physics tick rate to compensate


<!-- keywords: Material, material, 3D, import -->
### 3D import: external material links break when source file renames a material; extracted material preview not shown in dialog
Advanced 3D import gotchas:
- If a material is extracted to an external .tres file and then RENAMED in the source 3D software (Blender/Maya), Godot loses the link and creates a new default material. Must manually reassociate in Advanced Import Settings.
- The Advanced Import Settings dialog shows the ORIGINAL materials from the 3D file, not your customized extracted versions — edits are invisible until the scene is placed in another scene in the editor.
- Skeleton retargeting 'Overwrite Axis' can produce 'horrible results' if the original Bone Rest values are important (the docs literally say this). Test carefully on each model.


<!-- keywords: CanvasLayer, BEHIND, scene, Node, CanvasItem, canvas, Tree -->
### CanvasLayer: layer number determines draw order, NOT scene tree position; topmost scene node draws BEHIND
Canvas layer gotchas:
- CanvasLayer draw order depends ONLY on the layer number (int), not the node's position in the scene tree
- Within the same layer, nodes draw in scene tree order with TOPMOST nodes drawn BEHIND lower siblings — opposite of what many developers expect
- CanvasLayers are completely independent of 3D camera transform — they render at a fixed screen-space position regardless of camera movement
- InputEvent coordinates use a different coordinate system than CanvasItem coordinates — converting requires traversing the full transform chain: Window → Stretch → Global Canvas → Canvas Layer → Node


<!-- keywords: Resources, ONCE, SAME, instantiate, Resource, load -->
### Resources are loaded ONCE and shared — all instances reference the SAME resource; use duplicate() to get independent copy
One of the most common Godot gotchas: when you load() a Resource, the engine returns the SAME cached instance to everyone. Modifying it affects ALL users of that resource:
```gdscript
# BAD: both enemies share the same material; changing one changes both
enemy1.material = load('res://enemy.material')
enemy2.material = load('res://enemy.material')

# GOOD: each enemy gets its own independent copy
enemy1.material = load('res://enemy.material').duplicate()
enemy2.material = load('res://enemy.material').duplicate()
```
This applies to all Resource types: Materials, AudioStream, Font, etc. Especially critical for materials where you want per-instance color/texture changes.


<!-- keywords: POST, ORDER, Signals, Light, signal, _ready, Node, scene, light -->
### _ready() is POST-ORDER (children before parents); Signals fire even when paused; Light cull mask doesn't block shadows
Scene tree and processing gotchas:
- `_ready()` uses POST-ORDER traversal: children's _ready() is called BEFORE their parent's _ready(). `_process()` uses pre-order (parent before children). Don't assume _ready() order matches _process() order.
- `process_priority` overrides scene tree order — lower numbers execute FIRST regardless of position in tree
- Signals still FIRE when the SceneTree is paused — nodes that can't process can still receive signals. Check pause state in signal callbacks if needed.
- 3D Light cull mask does NOT prevent shadow casting — only affects which objects are LIT by the light. To stop an object casting shadows, set GeometryInstance3D > Cast Shadow = Off.


<!-- keywords: PSSM, TIMES, rendering -->
### PSSM shadow splits: objects spanning all 4 splits render 5 TIMES; alpha blend disables shadows and reflections
3D lighting and material gotchas:
- Directional lights with PSSM shadow splits (e.g., 4 splits): an object that spans ALL split boundaries renders 5 TIMES (once per split + 1 final). Large objects with 4-split PSSM have major shadow performance cost.
- Alpha Blending mode on StandardMaterial3D DISABLES: shadow casting, receiving reflections from ReflectionProbe, SSR, SSAO — use Alpha Scissor or Alpha Hash for effects that need these.
- Normal maps must be OpenGL-style (Y+ up, green channel up). DirectX-style normal maps (Y- down, green channel down) appear inverted. Enable 'Flip Green Channel' in import settings for DirectX maps.
- Compatibility renderer forces 16-bit (not 32-bit) shadow depth — lower shadow precision than Forward+/Mobile


<!-- keywords: SubViewport, BELOW, World3D, Node, Viewport, rendering -->
### SubViewport only captures nodes BELOW it in hierarchy; sharing World3D requires explicit assignment
Viewport hierarchy gotchas:
- SubViewport renders only nodes that are CHILDREN or DESCENDANTS of it. Nodes above or beside it in the scene tree are invisible to that viewport.
- SubViewport by default creates its own World3D — to share the parent viewport's world (e.g., for picture-in-picture), assign `sub_viewport.world_3d = get_viewport().world_3d`
- SubViewport by default creates its own World2D too — share it similarly if needed for minimap effects
- Debug Draw modes (wireframe, overdraw, etc.) are NOT supported in the Compatibility renderer — always appear as normal


<!-- keywords: SSAO, AMBIENT, SSIL, light -->
### SSAO only affects AMBIENT light (not direct); SSR/SSAO/SSIL/glow ignore off-screen and occluded geometry
Environment post-processing gotchas:
- SSAO darkens only AMBIENT-lit areas by default — it has zero effect on direct light. Use 'Light Affect' parameter to partially apply to direct light (physically incorrect but sometimes desired)
- SSR, SSAO, SSIL, and Glow all fail to account for geometry OUTSIDE the camera view or occluded by other opaque objects — can cause jarring visual pops as objects enter/leave view
- Transparent materials do NOT appear in Screen-Space Reflections — SSR only reflects opaque geometry
- Glow is nearly invisible by default — must either lower HDR Threshold (so non-overbright areas glow) or increase Bloom above 0.0
- HDR Panorama sky with very bright spots produces visible sparkles in reflections — enable 'HDR Clamp Exposure' in the texture import settings


<!-- keywords: StandardMaterial3D, Blender, collision, physics, Material, material, action, export, 3D, blender, import -->
### StandardMaterial3D: refraction is screen-space only; Blender exports with backface culling OFF; height maps add no collision
Standard material gotchas:
- Refraction is SCREEN-SPACE only — materials cannot refract off themselves, other transparent objects, or off-screen geometry. Opaque objects in front get incorrect 'refracted' edges.
- Blender exports materials with backface culling DISABLED by default — renders hidden backfaces needlessly. Enable backface culling in Blender or set it manually in Godot import.
- Height maps (parallax/depth) create a VISUAL ILLUSION only — they add no actual geometry for physics collision. Use HeightMapShape3D for collision.
- Alpha antialiasing (alpha to coverage) requires MSAA 3D set to at least 2x in Project Settings — without it, only fixed dithering applies
- Glow Map texture stretches to viewport aspect ratio — use an image matching your target aspect ratio (e.g., 16:9) to avoid distortion
- Render Priority cannot force opaque objects to render over each other — depth testing takes precedence over render priority


<!-- keywords: Autoload, NEVER, instantiate, scene, Node, queue_free, Resource, load, autoload -->
### Autoload nodes: NEVER call free() or queue_free() — crashes the engine; built-in resources still deduplicated across instances
Autoload and Resource gotchas:
- Calling free() or queue_free() on an Autoload node at runtime CRASHES the engine — autoloads must exist for the entire lifetime of the application
- Even resources saved as 'built-in' inside a .tscn file are DEDUPLICATED — instancing the same scene 10 times still gives all 10 instances the same resource object
- Custom Resource inner classes (defined with `class` inside a script) do NOT serialize properties — must define Resource subclasses as TOP-LEVEL scripts
- Custom Resource _init() must have ALL parameters with default values or the Inspector cannot create/edit instances


<!-- keywords: Pausing, Always, physics, Node -->
### Pausing: physics disabled even for Always-process nodes — must call set_active(true) on physics server
Pausing gotchas:
- When SceneTree is paused, physics is DISABLED for ALL nodes — even nodes with ProcessMode = ALWAYS. To re-enable physics for specific operations while paused, call `PhysicsServer3D.set_active(true)` (and reset after).
- Signals fire on PAUSED nodes — callback code executes even if the node cannot process. Check pause state manually in signal callbacks if needed.
- Process mode INHERIT (default) means 'whatever my parent does' — if no ancestor explicitly sets a non-Inherit mode, SceneTree.paused controls everything


<!-- keywords: DirectionalLight3D, Compatibility, rendering, OmniLight3D, SpotLight3D, light, 3D -->
### DirectionalLight3D position is irrelevant; shadows in Compatibility renderer change light to sRGB (alters appearance)
3D light and shadow gotchas:
- DirectionalLight3D POSITION in the scene has ZERO effect on lighting — only the node's rotation matters. Moving it anywhere produces identical results.
- Enabling shadows on lights in the Compatibility renderer causes the light to render in sRGB space instead of linear space, potentially dramatically altering the light's color/intensity appearance. Adjust Energy to compensate.
- Shadow atlas is SHARED across all visible DirectionalLights3D (up to 8). Each additional directional shadow reduces effective resolution for all others.
- SpotLight3D shadows stop working entirely at angles wider than 89 degrees — use OmniLight3D for wide-angle shadows
- Projector textures on lights MULTIPLY (darken) the light color — increase Energy to compensate for the darkening effect


<!-- keywords: Occlusion, AABB, MultiMesh, GPUParticles, MeshInstance3D, mesh, CSG, GPUParticles3D, particles -->
### Occlusion culling: entire AABB must be occluded; CSG/MultiMesh/GPUParticles ignored by bake; moving occluder = BVH rebuild
Occlusion culling gotchas:
- The occludee's ENTIRE AABB must be fully hidden by occluders for it to be culled — large objects with big AABBs are less likely to be culled
- CSG nodes (unless converted to MeshInstance3D), MultiMeshInstance3D, GPUParticles3D, and CPUParticles3D are IGNORED by the occluder bake. Must manually place OccluderInstance3D shapes for these.
- Moving an OccluderInstance3D at runtime (or its parent) triggers a BVH reconstruction EVERY FRAME — very expensive. Toggling visibility is much cheaper than moving.
- Moving static geometry after baking occluders causes incorrect culling (objects culled incorrectly) — must re-bake after ANY level geometry movement


<!-- keywords: Mesh, MultiMesh, SAME, instantiate, scene, mesh -->
### Mesh LOD: OBJ files skip LOD generation; all MultiMesh instances use the SAME LOD level simultaneously
LOD system gotchas:
- OBJ files are imported as individual mesh resources (not scenes), so LOD generation is SKIPPED. Change import type to 'Scene' and reimport to get LODs.
- MultiMeshInstance3D, GPUParticles3D, CPUParticles3D: ALL instances use the SAME LOD level at the same time (chosen by closest instance to camera). Instances far from the closest one get wrong LOD.
- MultiMesh instances spread far apart should be split into separate MultiMeshInstance3D nodes for correct per-group LOD and culling
- Mesh LOD automatically adjusts for camera FOV and viewport resolution — higher FOV or lower resolution makes LOD transitions more aggressive


<!-- keywords: BLOCKS, ResourceLoader, scene, load, Thread, threading -->
### change_scene_to_file() BLOCKS until fully loaded — no loading screen possible without ResourceLoader.load_threaded_request()
Scene management gotchas:
- change_scene_to_file() and change_scene_to_packed() block the main thread until the scene is completely loaded and ready — no progress indicator or animation is possible during this time
- For loading screens with progress: use ResourceLoader.load_threaded_request() to load in background, then change_scene_to_packed() once complete
- _exit_tree() is called in BOTTOM-TO-TOP order (children before parents) when a scene is being removed — opposite of the top-to-bottom order used when entering the tree


<!-- keywords: Decals, Mobile, Albedo, Mix, Forward, Mesh, mesh -->
### Decals: Mobile hard limit 8 per mesh; Albedo Mix doesn't reduce normal impact; Forward+ shares 512-element cluster limit
Decal gotchas:
- Mobile renderer: maximum 8 decals per mesh — extras silently don't render with no warning
- Albedo Mix controls color blending but does NOT reduce normal/ORM texture impact. For footprint or wet surface decals (normal-only): set Albedo Mix to 0.0
- Forward+ renderer: decals share a 512-element cluster limit with lights and ReflectionProbes. Hitting this limit silently stops decals from rendering.
- Normal Fade = 0.0 means decal projects at ALL angles (including backwards through surfaces); 0.999 limits to nearly perpendicular only — counterintuitive direction


<!-- keywords: Volumetric, light -->
### Volumetric fog: finite range only; temporal reprojection leaves ghosting trails from moving lights/volumes
Volumetric fog gotchas:
- Volumetric fog only covers a FINITE range and stops rendering at distance — for large world coverage, combine with non-volumetric exponential fog
- Moving FogVolumes or Light3Ds with volumetric fog enabled leave visible GHOSTING TRAILS due to temporal reprojection. Set Volumetric Fog Energy to 0.0 on short-lived dynamic lights.
- Sky Affect = 0.0 disables volumetric fog on the skybox AND prevents FogVolumes from affecting the sky background
- NoiseTexture3D density textures: only the RED channel of the color ramp affects density; other channels are ignored


<!-- keywords: Visibility, AABB, CENTER -->
### Visibility range uses AABB CENTER for distance calculation — off-center geometry causes unexpected LOD pops
Visibility range gotchas:
- Distance for visibility range Begin/End is measured from the AABB CENTER, not the closest mesh point. Geometry far off-center from its pivot causes unexpected LOD transitions.
- Fade Mode has NO visual effect if Begin Margin AND End Margin are both 0.0
- Manually hiding a Visibility Parent target (Visible = false) breaks the dependency — the node stops being hidden by the visibility range rule


<!-- keywords: ONCE, Node, add_child, _ready, scene, Tree, MULTIPLE, TIMES -->
### _enter_tree() can be called MULTIPLE TIMES (node re-added); _ready() is guaranteed exactly ONCE per lifetime
Node lifecycle gotchas:
- _enter_tree() fires EVERY TIME the node enters the scene tree — if a node is removed and re-added, _enter_tree() runs again. Don't put one-time initialization there.
- _ready() is called exactly ONCE in a node's lifetime (after the first _enter_tree()). Use it for one-time initialization.
- _exit_tree() fires every time the node leaves the tree and is received AFTER all children have already received their _exit_tree() (bottom-up order)


<!-- keywords: File, Windows, INSENSITIVE, Linux, Android, SENSITIVE, export -->
### File paths: Windows/macOS are case-INSENSITIVE (works), Linux/Android are case-SENSITIVE (silently fails on export)
Cross-platform file path gotchas:
- On Windows/macOS, `load('res://myfile.PNG')` works even if the file is `myfile.png`. On Linux/Android export, this SILENTLY FAILS. Always match case exactly in code.
- Moving or renaming assets using the OS file explorer (not Godot's FileSystem dock) breaks all references — Godot detects but doesn't prevent saving broken references
- res:// is READ-ONLY in exported builds — any attempt to write files to res:// works in editor but silently fails in exported games. Write game state to user:// instead.


<!-- keywords: NEVER, Default, Theme, Scale, Resource, load, preload, UI -->
### hide() keeps ALL processing active; preload() resources are NEVER unloaded; Default Theme Scale can't change at runtime
Scene management and UI gotchas:
- Using hide() to 'swap' scenes leaves all processing, physics, groups, and animations ACTIVE on the hidden scene. 10 hidden scenes = 10 fully processing scenes eating CPU/memory.
- preload() resources are baked into the compiled script and NEVER unloaded, even when all instances are freed. Using preload() for multiple large scenes causes permanent memory growth.
- `Project Settings > GUI > Theme > Default Theme Scale` is read-only at runtime. Use `get_tree().root.content_scale_factor` (a Window property) instead for runtime UI scaling.
- Regular 'Fullscreen' window mode leaves a 1-pixel line at the bottom, breaking integer pixel scale calculations. Use 'Exclusive Fullscreen' for correct integer scaling.


<!-- keywords: Resource, IMMEDIATELY, load, export -->
### @export vars return hardcoded default in _init() NOT inspector value; @export Resource loads IMMEDIATELY (hard dependency)
GDScript export gotchas:
- Reading an @export variable inside `_init()` returns the HARDCODED default (e.g., `= 2`), NOT the value set in the Inspector (e.g., `3`). Inspector values are applied AFTER _init(). Read @export vars in `_ready()` or via setters.
- `@export var texture: Texture2D` creates a HARD DEPENDENCY — the texture loads immediately when the scene loads, even if never used. Use `@export_file` for lazy/optional loading.
- @export getter/setter is IGNORED in the editor unless the script has `@tool`. Developers expecting setters to fire on inspector change are surprised they don't.
- `@export_storage` variables ARE copied when duplicating nodes/resources; non-exported variables are silently LOST on duplicate()


<!-- keywords: Physical, CameraAttributes, Camera, light -->
### Physical light units: enabling forces depth of field (GPU cost); missing CameraAttributes makes editor extremely bright
Physical light and camera unit gotchas:
- Enabling physical camera units (even just for exposure control) forces depth of field to be active, adding 'moderate' GPU performance cost even if blur is visually imperceptible
- Adding WorldEnvironment WITHOUT assigning a CameraAttributes resource while physical light units are enabled makes the 3D editor viewport EXTREMELY bright (functionally unusable)
- Default values (100,000 lux, f/16 aperture) are ONLY correct for outdoor daylight. Indoor/night scenes with these defaults show barely-visible lights — must adjust significantly.


<!-- keywords: AFTER, export, error -->
### @onready + @export on same variable is an error — @onready fires AFTER inspector values apply, overwriting them
GDScript initialization order gotchas:
- Combining `@onready` and `@export` on the same variable raises ONREADY_WITH_EXPORT warning/error by default — @onready initializes AFTER exported inspector values are applied, silently overwriting whatever was set in the Inspector
- Initialization order: class variables (top to bottom) → _init() → inspector values applied → @onready variables. Understanding this prevents subtle override bugs.
- Integer division silently truncates: `5 / 2 == 2` (not 2.5). Must write `5 / 2.0` or `float(5) / 2` to get a float result. No warning or error.
- GDScript `%` (modulo) uses TRUNCATION for negatives (like C), not floor division (like Python): `-7 % 3 == -1` in GDScript, but `2` in Python. Use `posmod(-7, 3)` for always-positive remainder.


<!-- keywords: Typed, Array, GDScript, typed, lambda -->
### Typed arrays cannot be assigned between subtypes; native method overrides silently fail; lambda captures are by value
GDScript type system gotchas:
- `Array[Node2D]` CANNOT be assigned to `Array[Node]` even though Node2D inherits Node. Use `.assign()` method to copy elements between typed arrays of related types.
- Overriding non-virtual engine methods (get_class(), queue_free(), etc.) SILENTLY FAILS — the engine calls its own implementation, not yours. Only override methods marked as `virtual` in the docs.
- Lambda variable captures are BY VALUE at creation time for primitives. Reassigning a captured variable outside the lambda doesn't update the lambda's copy. For reference types (Array, Dict, Object), changes to the object's contents ARE shared, but reassigning the variable is not.


<!-- keywords: Forward, SLOWER, Compatibility, Mobile, rendering, XR -->
### Forward+ is SLOWER than Compatibility on old hardware; 'Mobile' renderer NOT recommended for XR standalone; renderer auto-falls back silently
Renderer selection gotchas:
- Forward+ is the most feature-rich but WORST performing on old/low-end hardware — use Compatibility for low-end targets
- Despite its name, 'Mobile' renderer is only recommended for NEWER mobile devices. For standalone XR headsets (Quest, etc.), use Compatibility renderer.
- Since Godot 4.4, Forward+/Mobile automatically fall back to Compatibility if Vulkan/D3D12/Metal is unavailable — silently reduces quality. Disable in project settings if you want to show an error instead.


<!-- keywords: SpringArm3D, DIRECT, collision, Camera, camera -->
### SpringArm3D: camera must be DIRECT child for pyramid collision; any assigned shape disables the camera pyramid
SpringArm3D (camera collision) gotchas:
- SpringArm3D uses the camera's frustum pyramid shape for collision ONLY if the camera is a DIRECT child. Nested cameras (grandchildren) fall back to a simple raycast — inaccurate for camera collision.
- Assigning ANY shape to SpringArm3D's Shape property COMPLETELY DISABLES the camera pyramid detection, even if you intended them to work together. Remove the explicit shape to use the camera pyramid.


<!-- keywords: GDScript, Resource, Godot, load, gdscript, inner class -->
### GDScript: inner Resource classes defined with 'class' cannot be serialized — save/load fails because Godot can't find inner class by name
If you need a Resource subclass that can be saved to disk, define it as a SEPARATE .gd file with class_name. Inner classes (defined with 'class' inside another script) are invisible to Godot's resource saving system and cannot be deserialized from .tres/.res files.


<!-- keywords: Resource, MUST -->
### Resource._init() MUST have all parameters with default values or the inspector cannot create/edit it
If your custom Resource subclass has _init() with required parameters, the editor inspector will silently fail when trying to create or edit instances. Always give every _init() parameter a default value.


<!-- keywords: CONSTANT, String, error -->
### preload() requires a CONSTANT string literal — using a variable path causes a parse error
preload() is resolved at parse time, not at runtime. Passing a variable or expression as the path causes a syntax error. Use load() for dynamic paths, but preload() only accepts string literals.


<!-- keywords: GDScript, gdscript, static, int, enum -->
### GDScript enum types in static typing are just ints — no range validation, can hold any integer
Declaring a variable as an enum type (var state: MyEnum) only provides editor hints, not runtime enforcement. Any integer can be assigned without error. Use match with an else clause to catch unexpected values.


<!-- keywords: Deleting, CRASHES, signal, emit, scene, Node -->
### Deleting the current scene node inside a signal callback CRASHES — use call_deferred('free') instead
When a signal fires, the node that emitted it is still on the call stack. Calling free() or queue_free() on the scene root or an ancestor during the signal handler causes a crash. Defer cleanup: call_deferred("free") or set a flag and clean up in _process().


<!-- keywords: SubViewportContainer, CANNOT, SubViewport, rendering, Container, int -->
### SubViewportContainer CANNOT be sized smaller than its child SubViewport — size constraint is silently ignored
If you set SubViewportContainer smaller than the child SubViewport's size property, the container size setting is overridden. Set the SubViewport's size to control what's rendered; the container size follows.


<!-- keywords: Expression, int, float, GDScript -->
### Expression class: integer division (5/2=2) — pass floats explicitly to get float results
The Expression class performs integer division when both operands are integers, just like GDScript. '5 / 2' evaluates to 2, not 2.5. Pass '5.0 / 2' or cast in the expression string to get float division.


<!-- keywords: Expression, SILENTLY, instantiate -->
### Expression.execute() returns null SILENTLY when methods are called without a base_instance
If your expression calls instance methods (non-static functions) but you pass null as base_instance to execute(), the call returns null with no error. Always pass the object instance as base_instance when calling methods.


<!-- keywords: CASE, SENSITIVE, Node, get_node -->
### get_node() is CASE-SENSITIVE — renaming a node in the editor silently breaks all get_node() calls for it
There's no static analysis to catch get_node("MyNode") calls when the node is renamed to "myNode". Using @onready var x = $MyNode gives a parse-time error on scene load, which is better. But even @onready breaks if you rename the node.


<!-- keywords: FRAME, STALE, physics, _process, threading -->
### _process() reads physics state ONE FRAME STALE — physics runs before _process() in single-threaded mode
In single-threaded execution, _physics_process() runs before _process(). Physics body positions read in _process() reflect the physics state AFTER the step that just completed, but before the next one — so position is one physics frame behind current visual intent.


<!-- keywords: SAME, scene, Node, Tree -->
### process_priority: nodes with the SAME priority still execute in scene-tree order (not random)
process_priority controls ordering across different priority values. Nodes with identical priority values fall back to scene-tree order (parent before child, top to bottom). This is rarely documented and causes confusion when debugging execution order.


<!-- keywords: TREE, _ready, Tree -->
### _ready() guarantees children are IN THE TREE, but NOT that their _ready() has run yet
Since _ready() is post-order (children first), by the time a parent's _ready() runs, all children have already had their _ready() called. However, if a child was added DYNAMICALLY after initial setup, its _ready() fires when it's added, which may be after the parent's _ready().


<!-- keywords: Groups, Global, Scene, COSMETIC, scene, global -->
### Groups: 'Global' vs 'Scene' organization in editor is COSMETIC — same-named groups are identical at runtime
The editor groups dialog shows 'Global' and 'Scene' tabs for organizational purposes only. A group named 'enemies' in Scene tab and 'enemies' in Global tab are the SAME group at runtime. There is no runtime distinction.


<!-- keywords: IMMEDIATE, SYNCHRONOUS, Node -->
### call_group() is IMMEDIATE and SYNCHRONOUS — changing group membership during the call affects which nodes receive it
Unlike signals, call_group() calls the method on every group member synchronously in sequence. If a method handler adds or removes nodes from the group, those changes take effect immediately and affect subsequent members receiving the call in the same frame. Use call_group_flags(GROUP_CALL_DEFERRED) to avoid this.


<!-- keywords: Compositor, DIFFERS, rendering, Viewport, int -->
### Compositor effects: internal render size DIFFERS from viewport size when upscaling (FSR/TAA) is active
When using FSR2, TAA, or any upscaling, the compositor render buffer size is the INTERNAL resolution (smaller), not the final output resolution. Using viewport.get_size() inside a compositor effect gives wrong UV calculations. Use the texture's actual size instead.


<!-- keywords: Sync -->
### V-Sync with double-buffered swapchain (2 images): missing one frame drops GPU throughput to 50% of refresh rate
With V-Sync enabled and a 2-image swapchain, if the GPU takes longer than 16.6ms (at 60Hz), it misses the vsync window entirely and has to wait another full frame — causing 30 FPS not 59 FPS. Triple-buffering (3 images) reduces this effect but increases input latency.


<!-- keywords: Rendering, Shader, STILL, shader, rendering -->
### Rendering server: freeing a Shader RID auto-frees dependent pipelines, but you STILL must free the Shader RID
Godot's rendering server tracks shader→pipeline dependencies and frees pipelines when their parent shader is freed. However, the shader RID itself must still be explicitly freed with RenderingServer.free_rid() or it leaks. The auto-cleanup only applies to children, not the shader itself.


<!-- keywords: GDScript, SILENTLY, gdscript -->
### GDScript doc comments: tags with spaces before the colon SILENTLY fail — '@tutorial  :' does nothing
In GDScript documentation comments, tags like @tutorial, @param, @return must have zero spaces between the tag name and the colon. '@tutorial  : https://...' (with extra spaces) silently fails to parse and the link doesn't appear in the generated docs.


<!-- keywords: GDScript, PRIVATE, HIDDEN, gdscript -->
### GDScript: members starting with underscore are treated as PRIVATE and HIDDEN in generated documentation
The documentation generator treats any function or variable prefixed with underscore (e.g., _helper_method) as private and excludes it from the public API documentation, even if it has documentation comments. This is intentional but surprises developers trying to document semi-public methods.


<!-- keywords: NavigationAgent, IGNORED, NavigationAgent3D, navigation, NavigationMesh, navmesh, mesh -->
### NavigationAgent: agent radius is IGNORED by navmesh — you must manually shrink the baked navmesh to account for agent size
The agent_radius on NavigationAgent3D only affects RVO avoidance calculations. The navmesh itself has no knowledge of agent size — it describes where the center point can move. To prevent agents from clipping walls, set the 'Agent Radius' in NavigationMesh bake settings to shrink the walkable area before baking.


<!-- keywords: Navigation, AFTER, NavigationServer, navigation -->
### Navigation region layers changed at runtime only take effect AFTER the next NavigationServer sync frame
Changing navigation_layers on a NavigationRegion3D/2D at runtime does NOT take effect immediately. The NavigationServer processes the change at its next synchronization step (end of physics frame). Path queries issued in the same frame as the layer change may use the old layer mask.


<!-- keywords: NavigationMesh, FREEZE, CRASH, navigation, mesh -->
### NavigationMesh baking: too-small cell_size/cell_height can FREEZE or CRASH the editor — voxel count explodes
Setting cell_size to very small values (e.g., 0.001) causes the baking process to create billions of voxels, consuming all RAM and freezing or crashing Godot. Use the largest cell size that still captures the necessary detail. A cell_size of 0.25 is fine for most games.


<!-- keywords: Audio, NAME, Master, AudioStreamPlayer, audio -->
### Audio bus references stored by NAME — renaming a bus silently routes audio to Master instead
AudioStreamPlayer.bus is a string property storing the bus name. If you rename a bus in the Audio panel, all AudioStreamPlayers set to that bus silently fall back to 'Master' — there's no warning or error. Update bus names in all players after renaming.


<!-- keywords: Audio, DISABLES, AudioStreamPlayer, audio -->
### Audio bus DISABLES itself automatically after silence — effects stop processing too
Godot automatically disables audio buses after a few seconds of silence to save CPU. This means real-time audio effects (reverb, delay) on the bus also stop processing. If you need continuous effect processing (e.g., for ambient sound), keep a silent AudioStreamPlayer looping on the bus.


<!-- keywords: Web, AudioStreamPlayer, Sample, audio, export -->
### Web export: AudioStreamPlayer 'Sample' playback mode does NOT support audio bus effects
When Playback Type is set to 'Sample' (the recommended default for Web export performance), audio bus effects are silently skipped. Only 'Stream' playback type runs effects on Web. This means reverb, EQ, and other effects are silently absent in Web exports unless you switch playback type.


<!-- keywords: Controller, Windows, XInput, SILENTLY, Input, input -->
### Controller input: Windows supports max 4 XInput controllers — additional controllers are SILENTLY ignored
On Windows, Godot uses the XInput API which has a hard limit of 4 simultaneous controllers. Controllers 5 and beyond are silently not recognized. If you need more than 4, use the DirectInput backend (requires a plugin) or target Linux/macOS.


<!-- keywords: InputEventMouseMotion, CENTER, Input, input, mouse -->
### InputEventMouseMotion in MOUSE_MODE_CAPTURED: event.position returns screen CENTER, not cursor position — use event.relative
When the mouse is captured (Input.MOUSE_MODE_CAPTURED), the cursor is locked to the center of the screen. event.position will always return the screen center, not meaningful cursor coordinates. For FPS-style camera rotation, always use event.relative or event.screen_relative instead.


<!-- keywords: Controller, Button, pressed, keyboard, Input, InputEvent -->
### Controller buttons do NOT generate 'echo' events when held — keyboard repeat is NOT available for controllers
Keyboard keys generate repeated InputEvent when held down (echo=true). Controller buttons never do. If you need repeated fire or menu scroll from a held button, you must implement your own timer-based repeat using Input.is_action_pressed() in _process().


<!-- keywords: Transform3D -->
### Transform3D.translated() vs translated_local(): one is world-relative, the other is object-relative — mixing them silently moves objects wrong
translated(offset) applies the offset in PARENT/GLOBAL space (ignoring the object's rotation). translated_local(offset) applies in the OBJECT'S LOCAL space (relative to the object's own axes). Using translated() to move an object 'forward' in its local Z direction won't work if the object is rotated.


<!-- keywords: Array, SAME -->
### Array.pick_random() can return the SAME element consecutively — use shuffle bag pattern to guarantee unique ordering
pick_random() uniformly picks from all elements including the previously returned one. For game mechanics like random loot, music shuffle, or enemy sequences that shouldn't repeat, implement a shuffle bag: shuffle the array once, iterate through it, and re-shuffle only when exhausted.


<!-- keywords: INDEX, INDICES, Array -->
### rand_weighted() returns an INDEX, not a value — the weights array maps to array INDICES, not to the values themselves
RandomNumberGenerator.rand_weighted([0.5, 1.0, 2.0]) returns 0, 1, or 2 (the index). Index 2 is returned twice as often as index 1. You then use this index to look up your actual value array. A common mistake is passing the value array directly and expecting a value back.


<!-- keywords: NavigationLink, navigation, pathfinding, animation -->
### NavigationLink does NOT move agents — game code must provide all movement (teleport, animation, etc.)
A NavigationLink tells the pathfinder 'these two points are connected', but does NOTHING to move the agent across the gap. You must write code to detect when the agent reaches the link start, then teleport or animate it to the link end. Without this, agents will walk off ledges or into walls trying to follow the link.


<!-- keywords: NavigationLink, DISABLES, ITSELF, navigation, navmesh, mesh -->
### NavigationLink silently DISABLES ITSELF if no navmesh polygon is found within its search radius
When a NavigationLink is placed, it searches for the nearest navmesh polygon within its connection_radius. If none is found (link is too far from the navmesh), the link silently disables with no error. Check that both link endpoints are above or very near the navmesh surface.


<!-- keywords: Navigation, UNREACHABLE, navigation, pathfinding, Mesh, mesh -->
### Navigation path queries to UNREACHABLE targets are much slower than reachable ones — pathfinding exhausts the entire mesh
When a target is reachable, A* exits early once the goal is found. When the target is unreachable, pathfinding must visit EVERY polygon in the mesh to confirm unreachability. Having agents frequently query paths to unreachable positions can cause severe CPU spikes. Pre-validate reachability or throttle queries.


<!-- keywords: NavigationAgent, EVERY, FRAME, NavigationAgent3D, navigation, pathfinding -->
### NavigationAgent: setting target_position EVERY FRAME requests a new path every frame — only update when target moves significantly
NavigationAgent3D/2D requests a complete path recalculation whenever target_position changes. Setting it in _process() to a moving target triggers a full pathfind every frame. Only update target_position when the target moves more than path_desired_distance, or use a timer to throttle recalculation.


<!-- keywords: Audio, CHUNKS, AudioStreamPlayer, audio -->
### Audio: get_playback_position() updates in CHUNKS, not continuously — direct timing comparisons will drift
AudioStreamPlayer.get_playback_position() only updates when the audio thread processes a block (typically every 512-1024 samples). Between blocks, the value is stale. For precise audio synchronization, compensate using a Time.get_ticks_usec() delta relative to when you last sampled get_playback_position().


<!-- keywords: Retargeting, Bone, Rest, DIFFERS, Blender, Maya, animation, blender -->
### Retargeting animations: Bone Rest DIFFERS between Blender and Maya — animations from different software can't be shared without retargeting
Blender applies Edit Bone Orientation as Bone Rest; Maya does not. This means the same animation data produces different poses on models from different DCC tools. Godot's skeleton retargeting system can compensate, but you must enable it and configure 'Overwrite Axis' carefully — wrong settings produce garbage poses.


<!-- keywords: Motion, DISABLED, Input, input -->
### Controller motion sensors (gyroscope, accelerometer) are DISABLED by default — enable them with Input.set_joy_motion_sensors_enabled()
Controller gyroscope and accelerometer sensors must be explicitly enabled with Input.set_joy_motion_sensors_enabled(device, true) (check Input.has_joy_motion_sensors(device) first) or they produce no data and silently drain battery. Also: accelerometer reports GRAVITY by default, so values immediately after stopping still show movement from gravity cancellation.


<!-- keywords: Keyboard, SILENTLY, keyboard -->
### Keyboard ghosting: multiple simultaneous keys can be SILENTLY missed — test key layouts on physical hardware
Most keyboards can only detect 2-6 simultaneous key presses depending on the key matrix. Certain combinations (especially on the same keyboard row) silently fail to register. WASD+Shift+Space is a common problem combination. Test movement + action combinations on real hardware, not only in the editor.


<!-- keywords: Emulate, Touch, From, Mouse, mouse, Input -->
### 'Emulate Touch From Mouse' only works on NON-touchscreen devices — mouse does not perfectly emulate touch
The project setting 'Input Devices > Pointing > Emulate Touch From Mouse' lets you test touch in the editor on desktop. However, mouse doesn't generate multi-touch events, and the behavior differs from real touch (no pressure, no touch ID pooling). Always test touch-specific UI on a real touch device before release.


<!-- keywords: Image, Reimport, DISCARDS, import -->
### Image import: selecting another file before clicking 'Reimport' DISCARDS all pending import changes silently
In the Import panel, if you change import settings and then click on a different file in the FileSystem dock before clicking 'Reimport', all your changes are lost with no warning or undo. Always click Reimport immediately after changing import settings before navigating away.


<!-- keywords: Import, MUST, Control, import -->
### Import .import files MUST be in version control — .godot/imported/ folder should NOT be
The .import files (next to each asset) store import settings and must be committed to git. The generated cache in .godot/imported/ is machine-specific and should be in .gitignore. If .import files are missing, team members lose all custom import settings.


<!-- keywords: Lossy, DISK, VRAM, Texture, texture -->
### Lossy texture compression reduces DISK size but NOT video memory — VRAM usage is the same as lossless
VRAM compression (BPTC, ASTC, ETC2, S3TC) is what reduces GPU memory usage. 'Lossy' mode in the importer only affects the on-disk .import cache file size. A lossy PNG that imports as uncompressed VRAM will use the same GPU memory as a lossless PNG.


<!-- keywords: Normal, OpenGL, DirectX, INVERTED, Map, Invert -->
### Normal maps must be OpenGL-format (Y+, green=up) — DirectX-format normal maps appear INVERTED without 'Normal Map Invert Y'
Godot uses OpenGL normal map convention where green channel = Y+ (up). DirectX-format normal maps (from some bakers) have the Y axis flipped (green = Y-). Without enabling 'Normal Map Invert Y' in import settings, lighting will appear inverted on surfaces. Check your normal map baker's output format.


<!-- keywords: High, BPTC, ASTC, Forward, Mobile, S3TC, ETC2, Compatibility, Texture, texture -->
### High-quality texture compression (BPTC/ASTC) only works in Forward+/Mobile — silently uses S3TC/ETC2 in Compatibility
BPTC (BC7) for desktop and ASTC for mobile are only available in Forward+ and Mobile renderers. In Compatibility renderer, Godot silently falls back to S3TC (DXT5) or ETC2, which have lower quality. If targeting Compatibility, test texture quality with that renderer specifically.


<!-- keywords: DEPENDENT, _process, delta -->
### lerp() for smooth following in _process() is framerate-DEPENDENT — use exponential decay for framerate independence
A common pattern like 'position = lerp(position, target, 0.1)' runs slower on high framerates and faster on low framerates. For framerate-independent smoothing, use: position = position.lerp(target, 1.0 - exp(-SPEED * delta)). This gives identical visual results at any framerate.


<!-- keywords: Bezier -->
### Bezier curve t parameter does NOT map to constant speed — use tessellate() and sample by arc length for even movement
Moving from t=0 to t=1 at constant rate on a Bezier curve does NOT produce constant speed motion. The curve moves faster near control points and slower at extremes. For constant-speed movement along a curve, tessellate the curve into equal-length segments using Curve3D.tessellate() and interpolate by arc length.


<!-- keywords: SceneTree, BYPASSES, scene -->
### SceneTree.quit() BYPASSES NOTIFICATION_WM_CLOSE_REQUEST — custom cleanup code won't run
If you call get_tree().quit() directly, the engine exits immediately without firing NOTIFICATION_WM_CLOSE_REQUEST on any node. If you have cleanup code (save progress, close network connections) in that notification handler, it will be skipped. Trigger your cleanup manually before calling quit().


<!-- keywords: Custom, Web, rendering, mouse, Input -->
### Custom mouse cursor: max 256x256 on desktop, max 128x128 on Web — larger sizes cause rendering issues
Input.set_custom_mouse_cursor() has platform size limits. Web browsers enforce a 128x128 maximum; larger cursors are silently cropped or broken. Desktop platforms support up to 256x256 but behavior varies. Always provide cursors within these limits and test on the web platform explicitly.


<!-- keywords: Software, Sprite2D, LEAST, sprite -->
### Software cursor (Sprite2D) adds at LEAST one frame of latency vs hardware cursor — hardware cursor always feels snappier
Hiding the OS cursor and drawing a Sprite2D at mouse position adds lag equal to one render frame (16ms at 60 FPS) because position updates are tied to the frame cycle. Hardware cursors (set with Input.set_custom_mouse_cursor()) are updated by the OS compositor at display rate, not game framerate. Use hardware cursors for responsiveness.


<!-- keywords: Apply, Root, Scale, scene, Node, Mesh, mesh, animation, int, 3D, import -->
### 3D import: 'Apply Root Scale' bakes scale into meshes/animations but NOT into later-added child nodes in inherited scenes
When 'Apply Root Scale' is enabled in the 3D importer, scale is baked into mesh vertex data and animation tracks. However, any nodes you ADD to the inherited scene afterward are not scaled — only the nodes that existed at import time. This causes scale mismatches when manually adding nodes like collision shapes to an imported character.


<!-- keywords: Ensure, Tangents, BROKEN, 3D, import, warning -->
### 3D import: 'Ensure Tangents' disabled saves disk/memory but causes BROKEN normal/height maps with no warning
Tangent vectors are required for normal mapping and parallax mapping. Disabling 'Ensure Tangents' in the mesh importer will silently produce incorrect lighting results on any mesh that uses normal maps. Only disable it for meshes that provably use neither normal nor height maps.


<!-- keywords: Blender, SILENTLY, DISCARDED, Limit, Playback, animation, 3D, blender, import -->
### 3D import animation: keyframes outside the Blender playback range are SILENTLY DISCARDED if 'Limit Playback' is enabled
The 'Limit Playback' import option restricts animation import to the Blender timeline's preview range. Any keyframes outside this range are silently dropped. If the timeline range wasn't set correctly in Blender before export, you'll get truncated animations with no error in Godot.


<!-- keywords: NavigationServer, STILL, DISABLED, navigation, Node, global -->
### NavigationServer: -noimp suffix STILL works even when 'nodes/use_node_type_suffixes' is DISABLED globally
The -noimp suffix in 3D scene names (which removes nodes from the imported scene) is special — it cannot be globally disabled unlike other name suffixes. Even with node_type_suffixes disabled, nodes named with -noimp will still be removed on import.


<!-- keywords: NavigationPolygon, FAIL, EXACT, navigation, int, float -->
### NavigationPolygon: overlapping or intersecting outlines FAIL to bake silently — vertex merging requires EXACT float equality
NavigationPolygon outlines must not overlap or intersect. Also, for two navmesh regions to connect, their edge vertices must match exactly (bit-for-bit float equality). Even positions that appear identical in the editor but differ by a floating-point epsilon will prevent edge connection. Use snapping when placing nav regions.


<!-- keywords: NavigationRegion, navigation, NavigationRegion3D, navmesh, mesh -->
### NavigationRegion: changing scale does NOT update the navmesh on the server — must manually rebake
Scaling a NavigationRegion3D/2D node at runtime does not trigger a navmesh update on the NavigationServer. The navmesh is baked in local space at a specific time and remains at that scale. You must call bake_navigation_mesh() again after scaling to get a correctly-scaled navmesh.


<!-- keywords: NavigationRegion, NavigationAgent, navigation, NavigationRegion3D -->
### NavigationRegion: disabling/enabling at runtime does NOT update existing paths — agents keep old paths through disabled regions
When you toggle enabled on a NavigationRegion3D/2D, current agents are NOT notified. They continue following their last computed path even if it now passes through the disabled region. Force path recalculation by resetting NavigationAgent target_position after toggling regions.


<!-- keywords: NavigationRegion, SILENTLY, REMOVES, navigation -->
### NavigationRegion: assigning to a new map SILENTLY REMOVES it from the old map — regions can only belong to ONE map
A navigation region can only be registered to a single NavigationMap. Calling NavigationServer.region_set_map() with a new map automatically removes it from the previous map. There's no error or warning — it simply disappears from the old map.


<!-- keywords: Audio, Amplify, SILENTLY, Limiter, audio -->
### Audio effects: Amplify can SILENTLY clip audio — add a Limiter effect after any Amplify to prevent distortion
The Amplify audio effect applies gain without any clipping protection. Setting volume_db above 0 dB on Amplify will cause digital clipping if the audio signal was already at maximum. Always follow Amplify with a Limiter effect to prevent distortion, especially for dynamic audio content.


<!-- keywords: Navigation, REQUIRE, navigation, Mesh, mesh -->
### Navigation: different actor sizes REQUIRE separate navigation maps with separately-baked meshes — one map does not fit all
A navigation mesh is baked for a specific agent radius and height. You cannot use the same navmesh for large and small actors. Each actor size needs its own NavigationMap with its own baked NavigationMesh. Use NavigationServer3D.map_create() to create multiple maps and assign agents to the appropriate one.


<!-- keywords: Navigation, WRONG, navigation, pathfinding -->
### Navigation path_search_max_distance set too LOW causes paths to go in WRONG direction — not just short paths
The path_search_max_distance parameter on NavigationPathQueryParameters limits how far the pathfinder searches. If set too low, pathfinding may return a path to a local nearby point rather than toward the target, causing agents to walk the WRONG direction. Set this only as a deliberate optimization for known limited-range queries.


<!-- keywords: Text, Linux, SILENTLY, error -->
### Text-to-speech on Linux SILENTLY fails if system libraries missing — no error, just empty voice list
On Linux, DisplayServer.tts_get_voices() returns an empty array if the required speech-dispatcher or espeak-ng libraries are not installed. There's no error or warning — the TTS system just silently produces nothing. Always check if tts_get_voices().is_empty() before using TTS, and provide a graceful fallback.


<!-- keywords: Blender, DISABLED, rendering, export, blender, import, GLTF, performance -->
### Blender: backface culling is OFF by default — glTF exports with cull_mode=DISABLED, halving GPU render performance
Blender materials have backface culling disabled by default. When exported to glTF, Godot imports these with material cull_mode set to Disabled (renders both sides). This doubles the polygon fill rate for all meshes. Always enable backface culling in Blender material settings before exporting, or enable it in Godot's import settings.


<!-- keywords: Blender, Texture, texture, export, blender, import, GLTF -->
### Blender pre-3.2: emissive textures are NOT exported to glTF — must be manually re-linked after import
Blender versions before 3.2 do not write emissive texture data into glTF files. Emissive color values export fine, but texture references are lost. If your team uses Blender 3.1 or earlier, emissive textures must be manually assigned in Godot's imported material after each reimport.


<!-- keywords: Direct, Blender, Android, Web, blender, import, .blend -->
### Direct .blend file import requires Blender installed on ALL team members' machines — not available on Android/Web editor
Godot can import .blend files directly without manual export, but this requires Blender to be installed and accessible in the system PATH on every machine in the team. This silently fails (no asset) if Blender is not found. It's also completely unavailable in the Android editor and Web editor builds.


<!-- keywords: collision_layer, collision_mask -->
### Collision layer bitmask vs Inspector layer index
In code, `collision_layer` and `collision_mask` are BITMASKS, NOT the layer number shown in the Inspector.
Inspector Layer 1 = bitmask 1 (2^0)
Inspector Layer 2 = bitmask 2 (2^1)
Inspector Layer 3 = bitmask 4 (2^2)
Inspector Layer 4 = bitmask 8 (2^3)
So `collision_layer = 4` means Inspector Layer 3, NOT Layer 4. Use powers of 2.
Example: to put a body on Inspector Layer 2 and collide with Layer 1:
  body.collision_layer = 2   # bitmask for layer 2
  body.collision_mask  = 1   # bitmask for layer 1


<!-- keywords: collision_mask -->
### Default collision_mask = 1 misses non-default layers
New physics bodies always start with `collision_mask = 1` (only layer 1). If terrain/walls use layer 2+, the player falls through with NO error message. Always explicitly set `collision_mask` to include all layers the body must collide with.


<!-- keywords: CharacterBody3D, MOTION_MODE_FLOATING, motion_mode, floor_stop_on_slope -->
### CharacterBody3D: use MOTION_MODE_FLOATING for non-platformer 3D movement
For any 3D movement that is NOT a side-scroller (top-down, vehicles, boats, space), you MUST set `motion_mode = CharacterBody3D.MOTION_MODE_FLOATING`. The default GROUNDED mode applies `floor_stop_on_slope` which fights movement on slopes and breaks top-down games entirely.
CORRECT for top-down / vehicles / snowboards:
  body.motion_mode = CharacterBody3D.MOTION_MODE_FLOATING


<!-- keywords: _physics_process, target_position, follow_speed -->
### Camera lerp from origin glitch
Cameras using `lerp()` in `_physics_process()` will visibly swoop from `(0, 0, 0)` on the first frame. Fix with an `_initialized` flag:
```gdscript
var _initialized := false
func _physics_process(delta: float) -> void:
    if not _initialized:
        position = target_position  # snap on frame 1
        _initialized = true
        return
    position = position.lerp(target_position, follow_speed * delta)
```


<!-- keywords: drag_rate -->
### Frame-rate independent drag/damping
`speed *= (1 - drag)` per tick is frame-rate dependent (varies wildly between 60Hz and 120Hz). Use exponential decay instead:
WRONG (frame-rate dependent):  velocity *= (1.0 - drag)
CORRECT (frame-rate independent):  velocity *= exp(-drag_rate * delta)


<!-- keywords: BoxShape3D, CapsuleShape3D, SphereShape3D -->
### BoxShape3D snags on trimesh surfaces (use CapsuleShape3D instead)
BoxShape3D catches on internal collision edges of trimesh surfaces (Godot/Jolt limitation). For objects that slide across trimesh (vehicles, rolling objects, players on terrain), use CapsuleShape3D or SphereShape3D instead.


<!-- keywords: target_yaw, turn_speed -->
### Smooth yaw rotation — avoid 360-degree spin-around
`lerp()` on raw angles causes objects to spin 360 degrees when crossing the 0/2PI boundary. Always wrap the angle difference to `[-PI, PI]` first:
WRONG:  rotation.y = lerp(rotation.y, target_yaw, t)
CORRECT:
```gdscript
var diff: float = fmod(target_yaw - rotation.y + 3.0 * PI, TAU) - PI
rotation.y += diff * turn_speed * delta
```


<!-- keywords: CollisionShape -->
### 2D collision shape slightly smaller than tile for smooth movement
For grid-based 2D games, make the collision shape slightly smaller than the tile (e.g., 48px CollisionShape in a 64px grid). Without this, characters snag on corridor entrances and can't pass through 1-tile-wide corridors.


<!-- keywords: ProceduralSkyMaterial, DirectionalLight3D, WorldEnvironment, sky_mode, SKY_MODE_LIGHT_AND_SKY, SKY_MODE_LIGHT_ONLY -->
### ProceduralSkyMaterial — avoid multiple sun discs
If you add multiple DirectionalLight3D nodes and WorldEnvironment uses ProceduralSkyMaterial, multiple sun discs appear. Fix:
- Primary sun light:  `light.sky_mode = DirectionalLight3D.SKY_MODE_LIGHT_AND_SKY`
- Fill/secondary lights:  `light.sky_mode = DirectionalLight3D.SKY_MODE_LIGHT_ONLY`


<!-- keywords: SiblingA, data_changed, _on_data_changed, has_data, get_data -->
### Sibling signal timing in _ready()
`_ready()` fires on children in scene order. If sibling A emits a signal in its `_ready()` and sibling B hasn't connected yet, B misses the signal. Fix: after connecting in B's `_ready()`, check if A already has data and call the handler manually:
```gdscript
func _ready() -> void:
    %SiblingA.data_changed.connect(_on_data_changed)
    if %SiblingA.has_data():  # check if already populated
        _on_data_changed(%SiblingA.get_data())
```


<!-- keywords: autowrap_mode, TextServer, AutowrapMode, AUTOWRAP_WORD_SMART, AUTOWRAP_ARBITRARY, AUTOWRAP_WORD, AUTOWRAP_OFF -->
### Label.autowrap was replaced by autowrap_mode (Godot 4.x)
The old boolean `Label.autowrap` no longer exists. Use `Label.autowrap_mode` with the `TextServer.AutowrapMode` enum:
  WRONG:  label.autowrap = true
  CORRECT: label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART  # wraps at word boundaries
Other values: AUTOWRAP_ARBITRARY (any character), AUTOWRAP_WORD (word only), AUTOWRAP_OFF (no wrap).


<!-- keywords: TileMapLayer, get_coords_for_body_rid, physics_quadrant_size, get_cell_source_id -->
### TileMapLayer (Godot 4.3+) — physics quadrant chunking changes get_coords_for_body_rid()
In Godot 4.5+, `TileMapLayer` enables physics quadrant chunking by default (`physics_quadrant_size` is non-zero). This means `get_coords_for_body_rid(rid)` may return wrong tile coordinates if the body spans a quadrant boundary or if quadrant size is large. To get reliable per-tile coords from a physics body RID, either:
- Call `get_coords_for_body_rid()` as usual but verify with `get_cell_source_id(coords) != -1`
- Or set `physics_quadrant_size = 1` (the value that disables merging) to get exact cell coordinates if precision is critical.


<!-- keywords: TileMap, TileMapLayer, Node2D -->
### TileMap is deprecated — use TileMapLayer (Godot 4.3+)
The old `TileMap` node still exists for compatibility but is deprecated. All new projects should use one or more `TileMapLayer` nodes directly as children of the scene root or a Node2D. Each `TileMapLayer` represents one layer (previously `TileMap` had multiple layers).
WRONG:  var tm = TileMap.new()  # deprecated
CORRECT: var layer = TileMapLayer.new()  # one node per layer


<!-- keywords: get_rpc_config, get_node_rpc_config -->
### Node.get_rpc_config() renamed to get_node_rpc_config() (Godot 4.5)
For multiplayer RPC setup, the method was renamed:
  WRONG:  node.get_rpc_config()
  CORRECT: node.get_node_rpc_config()


<!-- keywords: RenderingServer, instance_reset_physics_interpolation, instance_set_interpolated, Node3D, physics_interpolation_mode, reset_physics_interpolation -->
### RenderingServer physics interpolation methods removed (Godot 4.5)
`RenderingServer.instance_reset_physics_interpolation()` and `RenderingServer.instance_set_interpolated()` were removed. Use node-level interpolation instead: enable `Node3D.physics_interpolation_mode` and call `Node3D.reset_physics_interpolation()` on the node directly.


<!-- keywords: AnimationPlayer, StringName, GDScript, current_animation, assigned_animation, current_animation_changed -->
### AnimationPlayer string properties are StringName in 4.6+ (GDScript auto-converts)
`AnimationPlayer.current_animation`, `assigned_animation`, and `autoplay` changed from String to StringName in Godot 4.6. GDScript auto-converts, so existing code still works. However in C# you must update types explicitly. The `current_animation_changed` signal parameter is also now StringName.


<!-- keywords: StandardMaterial3D, no_depth_test, TRANSPARENCY_ALPHA, forward_plus, TRANSPARENCY_DISABLED, shading_mode, SHADING_MODE_UNSHADED, render_priority -->
### StandardMaterial3D: no_depth_test + TRANSPARENCY_ALPHA = invisible
In `forward_plus` rendering mode, a `StandardMaterial3D` with BOTH `no_depth_test = true` AND transparency mode `TRANSPARENCY_ALPHA` set will be completely invisible. For UI overlays and billboards, use `transparency = TRANSPARENCY_DISABLED` + `shading_mode = SHADING_MODE_UNSHADED` instead.
For surfaces layered on terrain (roads on ground), offset vertically by 0.1–0.3m and set `render_priority = 1` to avoid Z-fighting.


<!-- keywords: ONREADY_WITH_EXPORT, label_path, NodePath, get_node -->
### @onready + @export on the same variable — @onready silently overwrites the exported value
Using both `@onready` and `@export` on the same variable is treated as an **error** (ONREADY_WITH_EXPORT). The `@export` value set in the inspector is discarded when `_ready()` fires because `@onready` runs then and overwrites it. NEVER combine them:
  WRONG:  @export @onready var label: Label = $Label  # export value ignored!
  CORRECT: @export var label_path: NodePath; @onready var label: Label = get_node(label_path)
  OR:      @onready var label: Label = $Label  # without @export


<!-- keywords: static_unload, class_name -->
### @static_unload does not currently work — scripts are never freed
The `@static_unload` annotation is supposed to allow a script's static data to be freed when no instances exist. Due to a current bug, scripts are NEVER freed even with this annotation. Do not rely on `@static_unload` for memory management. Place `@static_unload` at the very top of the script (before `class_name` and `extends`) if used, as it applies to the entire script including inner classes.


<!-- keywords: singleton_ref, get_singleton, MySingleton -->
### Static variables cannot use @export or @onready
Static class variables (`static var`) cannot be decorated with `@export` or `@onready`. They belong to the class, not to instances. Local variables also cannot be declared static. Static variables share their value across ALL instances.
  WRONG:  @export static var count: int = 0
  WRONG:  @onready static var singleton_ref = Engine.get_singleton("MySingleton")
  CORRECT: static var count: int = 0  # class-level, no annotation


<!-- keywords: export_storage, cached_data -->
### @export_storage — persist non-exported properties in scene files
`@export_storage` saves a property in `.tscn`/`.tres` files without showing it in the inspector. Useful for runtime state that should be saved with the scene but not edited directly. Different from `@export` (which shows in the inspector) and plain `var` (not saved).
  @export_storage var cached_data: Dictionary = {}


<!-- keywords: export_tool_button, IconName, _bake_button -->
### @export_tool_button — adds a clickable button in the Inspector (Godot 4.3+)
`@export_tool_button("Label", "IconName")` creates a button in the Inspector that calls a Callable when clicked. Only works in `@tool` scripts. The second argument is optional (icon name from the editor theme):
  @tool
  @export_tool_button("Bake", "Bake") var _bake_button = bake
  func bake() -> void: pass


<!-- keywords: export_custom, hint_string -->
### @export_custom — override display in Inspector without changing the type
`@export_custom(hint, hint_string)` allows a custom inspector hint for a property that cannot be achieved with standard @export annotations. The property type is unchanged.


<!-- keywords: PackedScene -->
### Typed arrays in @export: use Array[Type] directly
Exported typed arrays use bracket syntax. `Array[PackedScene]`, `Array[int]`, `Array[String]` all work. Inner classes can also be array element types:
  @export var scenes: Array[PackedScene] = []
  @export var scores: Array[int] = []
NOTE: Nested typed arrays like `Array[Array[int]]` are NOT supported — use `Array[Array]` for 2D arrays.


<!-- keywords: export_range, radians_as_degrees -->
### @export_range suffix — annotate unit directly on the slider
`@export_range(min, max, step, "suffix:UNIT")` shows a unit label on the slider in the Inspector:
  @export_range(0, 100, 1, "suffix:kg") var weight: float = 0.0
  @export_range(0, 360, 0.1, "radians_as_degrees") var angle: float = 0.0
The `"radians_as_degrees"` flag stores radians internally but displays and edits degrees in the Inspector.


<!-- keywords: name_list, INFERENCE_ON_VARIANT -->
### for-loop typed variable — type inference works in for loops (Godot 4.2+)
Loop variables can have explicit type annotations even when iterating an untyped array:
  for name: String in name_list:  # name is typed String
This avoids INFERENCE_ON_VARIANT warnings when the array is untyped.


<!-- keywords: _iter_init, _iter_next, _iter_get, my_obj -->
### Custom iterators — implement _iter_init, _iter_next, _iter_get
Objects can be iterable in `for` loops by implementing three methods:
  func _iter_init(arg) -> bool: ...  # returns false if empty
  func _iter_next(arg) -> bool: ...  # advances, returns false at end
  func _iter_get(arg) -> Variant: ... # returns current element
The `arg` parameter is passed from the `for` loop (e.g., `for x in my_obj.range(5):`).


<!-- keywords: NOTIFICATION_MOVED_IN_PARENT, NOTIFICATION_CHILD_ORDER_CHANGED -->
### Node.NOTIFICATION_MOVED_IN_PARENT deprecated — use NOTIFICATION_CHILD_ORDER_CHANGED
The constant `Node.NOTIFICATION_MOVED_IN_PARENT` is deprecated. The engine no longer sends it. Use `Node.NOTIFICATION_CHILD_ORDER_CHANGED` instead to detect reordering of children.


<!-- keywords: auto_translate, auto_translate_mode, can_auto_translate, AUTO_TRANSLATE_MODE_DISABLED -->
### Control.auto_translate deprecated — use Node.auto_translate_mode
The `auto_translate` property on Control is deprecated. Use `Node.auto_translate_mode` (inherited by all nodes) and `Node.can_auto_translate()` instead.
  WRONG:  control.auto_translate = false
  CORRECT: control.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED


<!-- keywords: LAYOUT_DIRECTION_LOCALE, LAYOUT_DIRECTION_APPLICATION_LOCALE, layout_direction -->
### Control.LAYOUT_DIRECTION_LOCALE deprecated — use LAYOUT_DIRECTION_APPLICATION_LOCALE
  WRONG:  control.layout_direction = Control.LAYOUT_DIRECTION_LOCALE
  CORRECT: control.layout_direction = Control.LAYOUT_DIRECTION_APPLICATION_LOCALE


<!-- keywords: get_theme_, add_theme_, theme_override_colors, font_color, add_theme_color_override, get_theme_color, add_theme_font_override, add_theme_font_size_override -->
### Control theme items — read with get_theme_*(), override with add_theme_*_override()
Theme items (colors, fonts, constants, icons, styles) are not ordinary properties. `control.get("theme_override_colors/font_color")` only returns a value when an override was set on that control — it never returns the value inherited from the theme. Read with get_theme_*() and write with add_theme_*_override():
  CORRECT: label.add_theme_color_override("font_color", Color.RED)
  CORRECT: label.get_theme_color("font_color")  # read
Theme override methods: `add_theme_color_override`, `add_theme_font_override`, `add_theme_font_size_override`, `add_theme_constant_override`, `add_theme_icon_override`, `add_theme_stylebox_override`. Remove with `remove_theme_*_override`.


<!-- keywords: push_unhandled_input, push_input, in_local_coords, SubViewport -->
### Viewport.push_unhandled_input() deprecated — use push_input()
`Viewport.push_unhandled_input(event)` is deprecated. Use `Viewport.push_input(event, in_local_coords)` instead. The deprecated version also does NOT propagate to embedded Window or SubViewport nodes.


<!-- keywords: AnimationPlayer, ANIMATION_PROCESS_, AnimationMixer, ANIMATION_PROCESS_PHYSICS, ANIMATION_CALLBACK_MODE_PROCESS_PHYSICS, ANIMATION_PROCESS_IDLE, ANIMATION_CALLBACK_MODE_PROCESS_IDLE, ANIMATION_PROCESS_MANUAL, ANIMATION_CALLBACK_MODE_PROCESS_MANUAL -->
### AnimationPlayer: old ANIMATION_PROCESS_* constants deprecated (use AnimationMixer)
The following AnimationPlayer constants and methods are deprecated in favour of AnimationMixer equivalents:
  DEPRECATED → CORRECT:
  AnimationPlayer.ANIMATION_PROCESS_PHYSICS → AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_PHYSICS
  AnimationPlayer.ANIMATION_PROCESS_IDLE    → AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_IDLE
  AnimationPlayer.ANIMATION_PROCESS_MANUAL  → AnimationMixer.ANIMATION_CALLBACK_MODE_PROCESS_MANUAL
  AnimationPlayer.ANIMATION_METHOD_CALL_DEFERRED  → AnimationMixer.ANIMATION_CALLBACK_MODE_METHOD_DEFERRED
  AnimationPlayer.ANIMATION_METHOD_CALL_IMMEDIATE → AnimationMixer.ANIMATION_CALLBACK_MODE_METHOD_IMMEDIATE
Properties `callback_mode_method`, `callback_mode_process`, `root_node` moved to AnimationMixer base class.


<!-- keywords: AnimationPlayer, play_section, set_section_with_markers, start_marker, end_marker, play_section_with_markers, loop_start -->
### AnimationPlayer: new section-based playback (Godot 4.7)
AnimationPlayer gained section-based playback in 4.7. You can play a sub-range of an animation:
  player.play_section("Run", 0.2, 0.8)  # play only 0.2s→0.8s of Run
  player.set_section_with_markers("start_marker", "end_marker")
  player.play_section_with_markers("Run", "loop_start", "loop_end")
New property `playback_auto_capture` (default: true) — automatically captures current pose before blending to a new animation. Set `playback_auto_capture_duration` to control blend time.


<!-- keywords: MeshInstance3D, NodePath, Skeleton3D, ProjectSettings, default_parent_skeleton_in_mesh_instance_3d -->
### MeshInstance3D.skeleton default changed in Godot 4.6 — may break rigged meshes
The default lookup behavior of `MeshInstance3D.skeleton` (NodePath) changed in 4.6. Old code that relied on auto-detection of a parent Skeleton3D will fail. Fix:
  Option 1: Enable ProjectSettings → animation/compatibility/default_parent_skeleton_in_mesh_instance_3d
  Option 2: Explicitly set skeleton path in code or Inspector


<!-- keywords: RigidBody2D, body_entered, body_exited, get_colliding_bodies, contact_monitor, max_contacts_reported -->
### RigidBody2D: contact monitoring requires both flags set
To receive `body_entered`/`body_exited` signals and use `get_colliding_bodies()`, you MUST set BOTH:
  body.contact_monitor = true
  body.max_contacts_reported = 4  # or however many contacts you need
Setting only one has no effect. Collision results are one physics frame delayed.


<!-- keywords: _shortcut_input, _unhandled_key_input, _unhandled_input, _gui_input, set_input_as_handled -->
### Viewport input propagation order (4.7)
Input events are dispatched in this order: `_input()` → `Control._gui_input()` → `_shortcut_input()` → `_unhandled_key_input()` → `_unhandled_input()`. Call `set_input_as_handled()` (or `accept_event()` in a Control) at any stage to stop the event from going further.


<!-- keywords: SceneTree, change_scene_to_file, change_scene, get_tree, my_scene, change_scene_to_packed, packed_scene, PackedScene, change_scene_to_node -->
### SceneTree.change_scene_to_file() replaces change_scene()
The old `SceneTree.change_scene(path)` method is gone. Use:
  get_tree().change_scene_to_file("res://my_scene.tscn")  # from path string
  get_tree().change_scene_to_packed(packed_scene)           # from PackedScene
  get_tree().change_scene_to_node(scene_node)               # from instantiated node


<!-- keywords: GDScript, Vector2, Vector3, Rect2, Transform2D, Transform3D -->
### Variant types (except Nil and Object) are NEVER null
In GDScript, `int`, `float`, `bool`, `Vector2`, `Vector3`, `Color`, `String`, `Array`, `Dictionary`, `Rect2`, `Transform2D`, `Transform3D`, `Basis`, `Quaternion`, `AABB`, `Plane`, and other built-in Variant types CANNOT be null. They always have a default zero-initialised value. Only variables of type `Object` (or an Object subclass) can be null. Do NOT check `if my_vector == null` or `if my_string == null` — it is always false. Check `Object`-typed variables for null before calling methods on them.
  WRONG:  if my_vec3 == null: return   # Vector3 is never null
  WRONG:  if my_dict == null: return   # Dictionary is never null
  CORRECT: if my_node == null: return  # Object/Node can be null


<!-- keywords: HashMap, HashSet, GDScript -->
### HashMap does NOT preserve insertion order — use Array or Dictionary
The internal C++ `HashMap` (and `HashSet`) do NOT guarantee insertion order — iteration order is effectively random. The GDScript `Dictionary`, however, DOES preserve insertion order (it is an ordered map). When writing GDScript, use `Dictionary` when order matters. When iterating a `Dictionary`, keys are yielded in insertion order. `Array` always maintains insertion order.


<!-- keywords: PackedByteArray, _thread_func, _shared_array -->
### Godot containers are NOT thread-safe — use Mutex for cross-thread access
`Array`, `Dictionary`, `String`, `PackedByteArray`, and all other Godot containers are NOT thread-safe. Reading or writing from multiple threads simultaneously without a `Mutex` causes crashes or undefined behavior. Use `Mutex.lock()`/`unlock()` or the `with Mutex` pattern when sharing data across threads.
  var _mutex := Mutex.new()
  func _thread_func() -> void:
      _mutex.lock()
      _shared_array.append(1)
      _mutex.unlock()


<!-- keywords: load_steps -->
### TSCN format: load_steps deprecated, uid:// paths mandatory in 4.6+
In `.tscn` and `.tres` files: the `load_steps` header field is deprecated in Godot 4.6 (it still parses but is no longer generated). The `uid://` scheme is the canonical resource reference format — every resource saved in 4.6+ gets a stable UID embedded in its file, and TSCN files prefer `uid://xxxxx "res://path"` references so the scene stays valid even if you rename the file. Do NOT hardcode bare `res://` paths in handwritten TSCN if you expect the file to be renamed. The `format=3` header field identifies Godot 4.x TSCN.


<!-- keywords: AnimationPlayer, AnimationMixer, Position3D, Rotation3D, Scale3D -->
### AnimationPlayer optimized 3D tracks — no per-keyframe easing or blending
When `AnimationMixer.deterministic = false` and the animation targets 3D nodes via optimized tracks (Position3D, Rotation3D, Scale3D), per-keyframe easing curves and transition types are IGNORED. The track uses simple linear interpolation. To use custom easing, switch to generic `value` tracks or set `deterministic = true`. This is a deliberate performance trade-off for 3D skeletal animation.


