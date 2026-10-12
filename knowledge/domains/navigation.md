---
id: navigation
order: 30
match: stem
keywords:
  - navigation
  - navmesh
  - pathfind
  - path_find
  - navigationregion
  - navigationagent
  - a_star
  - astar
  - 导航
  - 寻路
---

## Navigation System Specialization

### 2D Navigation Setup
```gdscript
# Create NavigationRegion2D
var nav_region = NavigationRegion2D.new()
nav_region.name = "NavigationRegion2D"
var nav_poly = NavigationPolygon.new()
nav_poly.add_outline(PackedVector2Array([Vector2(0,0), Vector2(500,0), Vector2(500,500), Vector2(0,500)]))
# make_polygons_from_outlines() is deprecated: bake the outlines instead.
NavigationServer2D.bake_from_source_geometry_data(nav_poly, NavigationMeshSourceGeometryData2D.new())
nav_region.navigation_polygon = nav_poly
parent.add_child(nav_region)
nav_region.owner = scene_root
```

### 3D Navigation Setup
```gdscript
var nav_region = NavigationRegion3D.new()
nav_region.name = "NavigationRegion3D"
var nav_mesh = NavigationMesh.new()
nav_mesh.agent_radius = 0.5
nav_mesh.agent_height = 1.8
nav_region.navigation_mesh = nav_mesh
parent.add_child(nav_region)
nav_region.owner = scene_root
# Bake at runtime:
# nav_region.bake_navigation_mesh()
```

### NavigationAgent (in game scripts)
```gdscript
# Attach NavigationAgent2D to a CharacterBody2D
var agent = NavigationAgent2D.new()
agent.name = "NavigationAgent2D"
agent.path_desired_distance = 4.0
agent.target_desired_distance = 4.0
character.add_child(agent)
agent.owner = scene_root
# In the game script's _physics_process:
# agent.target_position = target_pos
# var next = agent.get_next_path_position()
# velocity = position.direction_to(next) * speed
# move_and_slide()
```

### Calculate Path (runtime)
```gdscript
var path: PackedVector2Array = NavigationServer2D.map_get_path(
	get_world_2d().navigation_map, start_pos, end_pos, true)
```
