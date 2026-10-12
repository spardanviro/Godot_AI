---
id: 3d_scene
order: 90
match: stem
keywords:
  - mesh
  - meshinstance
  - camera3d
  - light3d
  - directionallight
  - omnilight
  - spotlight
  - environment
  - worldenvironment
  - gridmap
  - 网格
  - 灯光
  - 环境
---

## 3D Scene Setup Specialization

### Complete 3D Scene
```gdscript
var scene_root = EditorInterface.get_edited_scene_root()
var ur = EditorInterface.get_editor_undo_redo()

# Camera
var cam = Camera3D.new()
cam.name = "MainCamera"
cam.position = Vector3(0, 5, 10)
cam.rotation_degrees = Vector3(-25, 0, 0)

# Directional Light
var light = DirectionalLight3D.new()
light.name = "Sun"
light.rotation_degrees = Vector3(-45, -30, 0)
light.shadow_enabled = true

# Environment
var env_node = WorldEnvironment.new()
env_node.name = "WorldEnvironment"
var env = Environment.new()
env.background_mode = Environment.BG_SKY
var sky = Sky.new()
sky.sky_material = ProceduralSkyMaterial.new()
env.sky = sky
env.tonemap_mode = Environment.TONE_MAPPER_ACES
env.ssao_enabled = true
env_node.environment = env

# Floor mesh
var floor_inst = MeshInstance3D.new()
floor_inst.name = "Floor"
var plane = PlaneMesh.new()
plane.size = Vector2(20, 20)
floor_inst.mesh = plane
var floor_mat = StandardMaterial3D.new()
floor_mat.albedo_color = Color(0.4, 0.4, 0.4)
floor_inst.set_surface_override_material(0, floor_mat)
# Static body for collision
var sb = StaticBody3D.new()
sb.name = "FloorBody"
var cs = CollisionShape3D.new()
var box_shape = BoxShape3D.new()
box_shape.size = Vector3(20, 0.1, 20)
cs.shape = box_shape

for node in [cam, light, env_node, floor_inst, sb]:
	ur.create_action("Add " + node.name)
	ur.add_do_method(scene_root, "add_child", node)
	ur.add_do_property(node, "owner", scene_root)
	ur.add_do_reference(node)
	ur.add_undo_method(scene_root, "remove_child", node)
	ur.commit_action()
sb.add_child(cs)
cs.owner = scene_root
```

### GridMap (3D Tile-based levels)
```gdscript
var gridmap = GridMap.new()
gridmap.name = "GridMap"
gridmap.cell_size = Vector3(2, 2, 2)
# Assign a MeshLibrary resource
gridmap.mesh_library = load("res://resources/mesh_library.tres")
# Place items: set_cell_item(position, item_index, orientation)
gridmap.set_cell_item(Vector3i(0, 0, 0), 0)
gridmap.set_cell_item(Vector3i(1, 0, 0), 0)
```
