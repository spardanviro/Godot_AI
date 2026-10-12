---
id: batch
order: 70
match: stem
keywords:
  - batch
  - bulk
  - cross-scene
  - all scene
  - 批量
  - 跨场景
---

## Cross-Scene Batch Operations Specialization

### Iterate All Scene Files
```gdscript
var efs = EditorInterface.get_resource_filesystem()
var scenes: Array[String] = []
var stack: Array[String] = ["res://"]
while stack.size() > 0:
	var dir_path: String = stack.pop_back()
	var dir = DirAccess.open(dir_path)
	if not dir:
		continue
	dir.list_dir_begin()
	var file_name: String = dir.get_next()
	while file_name != "":
		var full_path: String = dir_path.path_join(file_name)
		if dir.current_is_dir() and not file_name.begins_with("."):
			stack.push_back(full_path)
		elif file_name.ends_with(".tscn") or file_name.ends_with(".scn"):
			scenes.push_back(full_path)
		file_name = dir.get_next()
	dir.list_dir_end()
print("Found ", scenes.size(), " scenes")
```

### Batch Modify Nodes Across Scenes
```gdscript
for scene_path in scenes:
	var packed: PackedScene = load(scene_path)
	var instance: Node = packed.instantiate()
	# Find nodes by type or name
	# Collect them with a loop — do not define helper functions in generated code.
	var targets: Array[Node] = []
	var pending: Array[Node] = [instance]
	while not pending.is_empty():
		var current: Node = pending.pop_back()
		if current.is_class("Light2D"):
			targets.push_back(current)
		pending.append_array(current.get_children())
	for node in targets:
		node.set("energy", 1.5)  # Modify property
	# Re-pack and save
	var new_packed = PackedScene.new()
	new_packed.pack(instance)
	ResourceSaver.save(new_packed, scene_path)
	instance.queue_free()

```

### Bulk Find by Group
```gdscript
var scene_root = EditorInterface.get_edited_scene_root()
var enemies = scene_root.get_tree().get_nodes_in_group("enemies")
for enemy in enemies:
	enemy.set("health", 100)
```
