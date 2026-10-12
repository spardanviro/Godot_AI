---
id: screenshot
order: 100
match: stem
keywords:
  - screenshot
  - capture
  - viewport_image
  - 截图
  - 屏幕截取
---

## Viewport Screenshot (EditorScript)

```gdscript
# Capture the editor's 3D viewport
var viewport = EditorInterface.get_editor_viewport_3d(0)
if viewport:
	var img: Image = viewport.get_texture().get_image()
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path("res://screenshots/"))
	img.save_png("res://screenshots/capture.png")
	print("Screenshot saved.")

# Capture the 2D viewport
var vp2d = EditorInterface.get_editor_viewport_2d()
if vp2d:
	var img2d: Image = vp2d.get_texture().get_image()
	img2d.save_png("res://screenshots/capture_2d.png")
```
