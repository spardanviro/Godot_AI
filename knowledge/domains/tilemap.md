---
id: tilemap
order: 20
match: stem
keywords:
  - tilemap
  - tileset
  - tile_map
  - tile_set
  - tile
  - 瓦片
  - 地图块
---

## TileMap Specialization

### Create TileMap with TileSet
```gdscript
var tilemap = TileMapLayer.new()  # Godot 4.3+ uses TileMapLayer
tilemap.name = "Ground"
var tileset = TileSet.new()
tileset.tile_size = Vector2i(16, 16)
# Add a source (atlas)
var source = TileSetAtlasSource.new()
source.texture = load("res://textures/tileset.png")
source.texture_region_size = Vector2i(16, 16)
tileset.add_source(source, 0)  # source_id = 0
tilemap.tile_set = tileset
```

### Batch Fill Rectangle
```gdscript
# Fill a rectangular area with one tile
var source_id: int = 0
var atlas_coords := Vector2i(0, 0)
for x in range(start_x, start_x + width):
	for y in range(start_y, start_y + height):
		tilemap.set_cell(Vector2i(x, y), source_id, atlas_coords)
```

### Query Used Cells
```gdscript
var used_cells: Array[Vector2i] = tilemap.get_used_cells()
var rect: Rect2i = tilemap.get_used_rect()
# Clear all cells
tilemap.clear()
```

IMPORTANT: In Godot 4.3+, TileMap is deprecated. Use TileMapLayer instead. Each TileMapLayer represents one layer.
