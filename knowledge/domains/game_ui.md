---
id: game_ui
order: 120
match: stem
keywords:
  - hud
  - health bar
  - healthbar
  - hp bar
  - hpbar
  - mana bar
  - manabar
  - stamina bar
  - staminabar
  - resource bar
  - skill bar
  - skillbar
  - cooldown
  - skill slot
  - inventory
  - item slot
  - itemslot
  - item grid
  - dialog box
  - dialogue box
  - dialog system
  - typewriter
  - minimap
  - mini map
  - game ui
  - game hud
  - overlay ui
  - floating text
  - damage number
  - buff icon
  - status effect
  - canvaslayer
  - canvas layer
  - 血量框
  - 血桥
  - 血量条
  - 技能栏
  - 技能冷却
  - 背包
  - 物品格
  - 对话桥
  - 浮动文字
  - 伤害数字
  - 小地图
  - 游戏界面
  - 抬头显示
---

## Game UI Specialization (Godot 4 Control System)

### Unity vs Godot UI mapping
- Canvas (Overlay) → CanvasLayer (layer = 5 for HUD, 10 for menus, 100 for effects)
- Canvas (World Space) → Label3D with billboard = BILLBOARD_ENABLED
- RectTransform anchors → set_anchors_preset() or anchor_left/right/top/bottom
- Image → ColorRect (solid color) / TextureRect (texture) / Panel (styled)
- TextMeshPro → Label / RichTextLabel (bbcode_enabled = true for formatting)
- VerticalLayoutGroup → VBoxContainer
- HorizontalLayoutGroup → HBoxContainer
- GridLayoutGroup → GridContainer (set .columns)
- ScrollRect → ScrollContainer
- CanvasScaler → Project Settings > Display > Window > Stretch

### Anchor presets — always use these instead of manual anchor values
```gdscript
control.set_anchors_preset(Control.PRESET_TOP_LEFT)      # HP bar
control.set_anchors_preset(Control.PRESET_TOP_RIGHT)     # minimap
control.set_anchors_preset(Control.PRESET_BOTTOM_LEFT)   # chat log
control.set_anchors_preset(Control.PRESET_BOTTOM_RIGHT)  # action buttons
control.set_anchors_preset(Control.PRESET_CENTER_BOTTOM) # skill bar
control.set_anchors_preset(Control.PRESET_CENTER)        # crosshair, dialog
control.set_anchors_preset(Control.PRESET_FULL_RECT)     # fullscreen bg/effect
# After preset, set pixel offsets:
control.offset_left = 20; control.offset_top = 20  # margin from anchor
```

### CanvasLayer layering
```gdscript
var hud_layer = CanvasLayer.new()
hud_layer.layer = 5    # game HUD
var menu_layer = CanvasLayer.new()
menu_layer.layer = 10  # pause / main menu
var fx_layer = CanvasLayer.new()
fx_layer.layer = 100   # damage flash, vignette
```

### Health bar — correct pattern
Use ColorRect for fill bars (NOT ProgressBar unless you need dragging).
The fill ColorRect width = parent.size.x * (current / max).
```gdscript
# Scene structure: HBoxContainer > [HpLabel, BarContainer > [BG, DelayedFill, Fill], ValueLabel]
var bar_container = Control.new()
bar_container.custom_minimum_size = Vector2(180, 20)
bar_container.size_flags_horizontal = Control.SIZE_EXPAND_FILL
var bg = ColorRect.new(); bg.set_anchors_preset(Control.PRESET_FULL_RECT)
bg.color = Color(0.1, 0.1, 0.1, 0.85)
var delayed = ColorRect.new(); delayed.set_anchors_preset(Control.PRESET_FULL_RECT)
delayed.color = Color(0.8, 0.2, 0.2)  # red, lags behind
var fill = ColorRect.new(); fill.set_anchors_preset(Control.PRESET_FULL_RECT)
fill.color = Color(0.2, 0.8, 0.2)     # green, current HP
# In runtime script: fill.size.x = bar_container.size.x * ratio
```

### Skill slot with radial cooldown
```gdscript
# Panel (64x64) > [TextureRect icon, TextureProgressBar overlay, Label cd_label, Label key_hint]
var slot = Panel.new(); slot.custom_minimum_size = Vector2(64, 64)
var icon = TextureRect.new()
icon.set_anchors_preset(Control.PRESET_FULL_RECT)
icon.expand_mode = TextureRect.EXPAND_FIT_WIDTH_PROPORTIONAL
var overlay = TextureProgressBar.new()
overlay.set_anchors_preset(Control.PRESET_FULL_RECT)
overlay.fill_mode = TextureProgressBar.FILL_CLOCKWISE
overlay.tint_progress = Color(0, 0, 0, 0.75)
overlay.value = 0.0; overlay.max_value = 1.0
overlay.mouse_filter = Control.MOUSE_FILTER_IGNORE
# Runtime: overlay.value = cooldown_remaining / cooldown_duration
```

### Inventory grid
```gdscript
# ScrollContainer > GridContainer (columns=6)
var scroll = ScrollContainer.new()
scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
var grid = GridContainer.new(); grid.columns = 6
grid.add_theme_constant_override("h_separation", 4)
grid.add_theme_constant_override("v_separation", 4)
# Each item slot: Panel (64x64) > [ColorRect bg, TextureRect icon, Label stack_count]
# Rarity colors: common=gray, uncommon=green, rare=blue, epic=purple, legendary=orange
```

### Floating damage numbers — use Tween, NOT _process
```gdscript
# In a damage_number.gd script attached to a Node2D with a Label child:
func setup(damage: int, is_crit: bool = false) -> void:
	$Label.text = str(damage)
	if is_crit: $Label.add_theme_font_size_override("font_size", 36)
	var tween: Tween = create_tween().set_parallel()
	tween.tween_property(self, "position", position + Vector2(randf_range(-20,20), -80), 1.2).set_ease(Tween.EASE_OUT)
	tween.tween_property($Label, "modulate:a", 0.0, 1.2).set_delay(0.5)
	tween.chain().tween_callback(queue_free)
```

### Dialog typewriter effect — use Timer/Signal, NOT yield
```gdscript
# RichTextLabel with bbcode_enabled = true supports [color], [b], [wave] etc.
# Use visible_characters property for typewriter effect:
func show_dialog(text: String) -> void:
	$RichTextLabel.text = text
	$RichTextLabel.visible_characters = 0
	var tween = create_tween()
	tween.tween_property($RichTextLabel, "visible_characters", len(text), len(text) / 30.0)
```

### StyleBox — for panel/button styling
```gdscript
var style = StyleBoxFlat.new()
style.bg_color = Color(0.05, 0.05, 0.1, 0.92)
style.corner_radius_top_left    = 8
style.corner_radius_top_right   = 8
style.corner_radius_bottom_left = 8
style.corner_radius_bottom_right = 8
style.border_color = Color(0.4, 0.4, 0.6)
style.set_border_width_all(1)
panel.add_theme_stylebox_override("panel", style)
```

### Common mistakes to avoid
- NEVER set control.size in _ready() — use custom_minimum_size or set_deferred("size", ...)
- NEVER add UI nodes directly to game world nodes — always use CanvasLayer
- NEVER use control.position for responsive layout — use anchors_preset + offsets
- RichTextLabel: always set bbcode_enabled = true before using BBCode tags
- TextureProgressBar FILL_CLOCKWISE: value 0 = empty, 1 = full (invert for cooldown)
- Safe area (notch): use DisplayServer.get_display_safe_area() to get margins
