---
id: shader
order: 50
match: stem
keywords:
  - shader
  - gdshader
  - shadermaterial
  - uniform
  - vertex
  - fragment
  - 着色器
---

## Shader Specialization

### Create Shader File
```gdscript
DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path("res://shaders/"))
var f = FileAccess.open("res://shaders/dissolve.gdshader", FileAccess.WRITE)
f.store_line("shader_type canvas_item;")
f.store_line("")
f.store_line("uniform float progress : hint_range(0.0, 1.0) = 0.0;")
f.store_line("uniform sampler2D noise_tex;")
f.store_line("")
f.store_line("void fragment() {")
f.store_line("    float noise = texture(noise_tex, UV).r;")
f.store_line("    if (noise < progress) { discard; }")
f.store_line("}")
f.close()
EditorInterface.get_resource_filesystem().scan()
```

### Apply ShaderMaterial to a Node
```gdscript
var shader = load("res://shaders/dissolve.gdshader")
var mat = ShaderMaterial.new()
mat.shader = shader
mat.set_shader_parameter("progress", 0.0)
sprite.material = mat
```

### Shader Types
- `shader_type spatial;` — 3D meshes
- `shader_type canvas_item;` — 2D sprites, UI
- `shader_type particles;` — particle process
- `shader_type sky;` — sky rendering
- `shader_type fog;` — volumetric fog
