---
id: ui
order: 0
match: word
keywords:
  - 界面
  - 菜单
  - 按钮
  - 标签
  - 输入框
  - 文本
  - 布局
  - 面板
  - 对话框
  - 血条
  - 背包
  - 物品栏
  - 设置界面
  - ui
  - gui
  - hud
  - menu
  - button
  - label
  - panel
  - dialog
  - inventory
  - health bar
  - progress bar
  - text input
  - dropdown
  - slider
  - checkbox
  - tab
  - container
  - layout
  - control
  - widget
  - main menu
  - settings menu
  - pause menu
  - game over
  - score
  - minimap
  - tooltip
  - popup
---

## UI/Control Node Specialization
The user is requesting UI work. Use these Control node best practices:

### Container Hierarchy
- Use containers (VBoxContainer, HBoxContainer, GridContainer, MarginContainer) for automatic layout.
- Nest containers: MarginContainer > VBoxContainer > HBoxContainer for proper spacing.
- Use PanelContainer for backgrounds with StyleBoxFlat.
- Use ScrollContainer for scrollable content.

### Anchors and Sizing
- Use anchors for responsive positioning: anchors are floats from 0 to 1 — ANCHOR_BEGIN (0), ANCHOR_END (1), and plain 0.5 for the center (there is no ANCHOR_CENTER constant).
- set_anchors_preset(Control.PRESET_FULL_RECT) for fullscreen UI.
- set_anchors_preset(Control.PRESET_CENTER_TOP) for centered top elements.
- set_anchors_preset(Control.PRESET_BOTTOM_LEFT) for HUD elements.
- size_flags_horizontal/vertical: SIZE_EXPAND_FILL, SIZE_SHRINK_CENTER, SIZE_FILL.

### Common UI Patterns
#### HUD (Head-Up Display):
```gdscript
var canvas = CanvasLayer.new()
canvas.name = "HUD"
canvas.layer = 10
var margin = MarginContainer.new()
margin.set_anchors_preset(Control.PRESET_FULL_RECT)
margin.add_theme_constant_override("margin_left", 20)
margin.add_theme_constant_override("margin_top", 20)
margin.add_theme_constant_override("margin_right", 20)
margin.add_theme_constant_override("margin_bottom", 20)
canvas.add_child(margin)
```

#### Centered Dialog/Panel:
```gdscript
var panel = PanelContainer.new()
panel.set_anchors_preset(Control.PRESET_CENTER)
panel.custom_minimum_size = Vector2(400, 300)
var vbox = VBoxContainer.new()
panel.add_child(vbox)
var title = Label.new()
title.text = "Title"
title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
vbox.add_child(title)
```

### Theme and Styling
- Use add_theme_color_override(), add_theme_font_size_override(), add_theme_constant_override() for inline styling.
- Create StyleBoxFlat for custom backgrounds: var style = StyleBoxFlat.new(); style.bg_color = Color(...)
- Apply with add_theme_stylebox_override("panel", style)

### Input Handling for UI
- Buttons: connect("pressed", callable)
- LineEdit: connect("text_submitted", callable)
- Slider/SpinBox: connect("value_changed", callable)
- For editor scripts, use print() to show what would happen on interaction.

