# Godot Game UI Design Skill

Godot 4 のControl ノードシステムを使ったゲームUIデザイン。HUD、ヘルスバー、インベントリ、スキルバー、ダイアログ等のゲームUI要素の実装パターンを提供する。このSkillが適用される場合: ゲームUI設計、HUD作成、モバイルUI、アンカー設定、CanvasLayer設定。

---

## Quick Start：UIシーン構成の基本

GodotのUI階層はUnityのCanvas/RectTransformではなく、**CanvasLayer + Control ノード**で構成する。

```gdscript
# HUDシーンの基本構成（EditorScript）
var hud_root = CanvasLayer.new()
hud_root.name = "HUD"
hud_root.layer = 10  # Unityの sortingOrder に相当

var safe_area = MarginContainer.new()
safe_area.name = "SafeArea"
safe_area.set_anchors_preset(Control.PRESET_FULL_RECT)
hud_root.add_child(safe_area)
safe_area.set_owner(hud_root)
```

### Godot vs Unity 対応表

| Unity | Godot | 用途 |
|-------|-------|------|
| Canvas (Overlay) | CanvasLayer | UIのルート |
| Canvas (World Space) | Node3D 配下の Control | ワールドUI |
| RectTransform.anchorMin/Max | `anchor_left/right/top/bottom` or `anchors_preset` | レイアウト |
| CanvasScaler | `get_viewport().get_visible_rect()` / stretch設定 | レスポンシブ |
| Image | TextureRect / ColorRect / Panel | 画像表示 |
| TextMeshProUGUI | Label / RichTextLabel | テキスト |
| Button | Button | ボタン |
| Slider | ProgressBar / Slider | バー |
| VerticalLayoutGroup | VBoxContainer | 縦並び |
| HorizontalLayoutGroup | HBoxContainer | 横並び |
| GridLayoutGroup | GridContainer | グリッド |
| ScrollRect | ScrollContainer | スクロール |
| ContentSizeFitter | `size_flags_vertical = SIZE_SHRINK_CENTER` / fit_content | 自動サイズ |

---

## Game UI Types（ゲームUI分類）

### 1. Non-Diegetic UI（HUD）— 最頻出

プレイヤーだけが見えるオーバーレイUI。

```gdscript
# HUD CanvasLayer（画面最前面）
var hud = CanvasLayer.new()
hud.name = "HUDLayer"
hud.layer = 10  # 数値が大きいほど前面

# Safe Area マージン（ノッチ対応）
var safe = MarginContainer.new()
safe.name = "SafeArea"
safe.set_anchors_preset(Control.PRESET_FULL_RECT)
# マージンはランタイムスクリプトで DisplayServer.get_display_safe_area() から設定
hud.add_child(safe)
safe.set_owner(hud)
```

### 2. Diegetic UI（ワールドUI）

3D/2D空間内に存在するUI（敵頭上HPバーなど）。

```gdscript
# 敵頭上HPバー：Node3D子ノードとして SubViewport は使わず
# Godot 4 では Node3D に add_child(Control) は不可。
# 代わりに Billboard ラベルまたは WorldEnvironment + CanvasLayer を使う。
# 推奨: Label3D または MeshInstance3D + ShaderMaterial でビルボード表示
var label = Label3D.new()
label.name = "HPLabel"
label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
label.text = "100/100"
label.font_size = 24
enemy.add_child(label)
label.set_owner(enemy)
label.position = Vector3(0, 2.2, 0)
```

### 3. Meta UI（全画面エフェクト）

被ダメージフラッシュ、ビネット等。

```gdscript
# 最前面CanvasLayer + 全画面ColorRect
var effect_layer = CanvasLayer.new()
effect_layer.name = "EffectLayer"
effect_layer.layer = 100  # 最前面

var flash = ColorRect.new()
flash.name = "DamageFlash"
flash.set_anchors_preset(Control.PRESET_FULL_RECT)
flash.color = Color(1, 0, 0, 0)  # 透明な赤（ランタイムでアニメーション）
flash.mouse_filter = Control.MOUSE_FILTER_IGNORE  # クリック透過
effect_layer.add_child(flash)
flash.set_owner(effect_layer)
```

---

## HUD Screen Layout（HUD画面配置）

### 標準配置図

```
┌─────────────────────────────────────────────────────┐
│  [HP/MP Bar]              [Mini Map] [Settings]     │  ← 上部
│                                                     │
│  [Quest]                               [Buff Icons] │  ← 中上部
│                                                     │
│                      [照準]                         │  ← 中央
│                                                     │
│  [Chat]                              [Inventory]    │  ← 中下部
│                                                     │
│          [Skill Bar]    [Action Buttons]            │  ← 下部
└─────────────────────────────────────────────────────┘
```

### Godot アンカープリセット一覧

| 位置 | anchors_preset | 配置するUI |
|------|---------------|-----------|
| 左上 | PRESET_TOP_LEFT | HP/MP/スタミナ |
| 右上 | PRESET_TOP_RIGHT | ミニマップ、設定 |
| 左下 | PRESET_BOTTOM_LEFT | チャット |
| 右下 | PRESET_BOTTOM_RIGHT | インベントリ、ボタン |
| 下部中央 | PRESET_CENTER_BOTTOM | スキルバー |
| 中央 | PRESET_CENTER | 照準、ダイアログ |
| 全画面 | PRESET_FULL_RECT | 背景、エフェクト |

### HUDレイアウト実装例（EditorScript）

```gdscript
# HUDの基本レイアウト作成
var hud_root = CanvasLayer.new()
hud_root.name = "HUD"
hud_root.layer = 10

# HP バー（左上）
var hp_bar_container = HBoxContainer.new()
hp_bar_container.name = "HPBarArea"
hp_bar_container.set_anchors_preset(Control.PRESET_TOP_LEFT)
hp_bar_container.position = Vector2(20, 20)
hud_root.add_child(hp_bar_container)
hp_bar_container.set_owner(hud_root)

# ミニマップ（右上）
var minimap_container = Panel.new()
minimap_container.name = "MinimapArea"
minimap_container.set_anchors_preset(Control.PRESET_TOP_RIGHT)
minimap_container.custom_minimum_size = Vector2(150, 150)
minimap_container.position = Vector2(-170, 20)
hud_root.add_child(minimap_container)
minimap_container.set_owner(hud_root)

# スキルバー（下部中央）
var skill_bar = HBoxContainer.new()
skill_bar.name = "SkillBar"
skill_bar.set_anchors_preset(Control.PRESET_CENTER_BOTTOM)
skill_bar.position = Vector2(-200, -80)  # アンカーからのオフセット
hud_root.add_child(skill_bar)
skill_bar.set_owner(hud_root)
```

---

## Game UI Elements（ゲームUI要素）

### 1. ヘルスバー / リソースバー

遅延ダメージ表示（ダメージ時に赤ゲージが徐々に減少）。

#### ノード構造

```
HealthBar (HBoxContainer)
├── HPLabel (Label)           - "HP"
├── BarContainer (Control)
│   ├── Background (ColorRect) - 背景（暗色）
│   ├── DelayedFill (ColorRect) - 遅延ゲージ（赤）
│   └── Fill (ColorRect)       - 現在値ゲージ（緑）
└── ValueLabel (Label)        - "100/100"
```

#### EditorScript実装

```gdscript
# ヘルスバー作成
var health_bar = HBoxContainer.new()
health_bar.name = "HealthBar"
health_bar.custom_minimum_size = Vector2(220, 28)

var bar_container = Control.new()
bar_container.name = "BarContainer"
bar_container.custom_minimum_size = Vector2(180, 24)
bar_container.size_flags_horizontal = Control.SIZE_EXPAND_FILL

var bg = ColorRect.new()
bg.name = "Background"
bg.set_anchors_preset(Control.PRESET_FULL_RECT)
bg.color = Color(0.1, 0.1, 0.1, 0.85)
bar_container.add_child(bg)
bg.set_owner(bar_container)

var delayed_fill = ColorRect.new()
delayed_fill.name = "DelayedFill"
delayed_fill.set_anchors_preset(Control.PRESET_FULL_RECT)
delayed_fill.color = Color(0.8, 0.2, 0.2, 1.0)  # 赤
bar_container.add_child(delayed_fill)
delayed_fill.set_owner(bar_container)

var fill = ColorRect.new()
fill.name = "Fill"
fill.set_anchors_preset(Control.PRESET_FULL_RECT)
fill.color = Color(0.2, 0.8, 0.2, 1.0)  # 緑
bar_container.add_child(fill)
fill.set_owner(bar_container)

var value_label = Label.new()
value_label.name = "ValueLabel"
value_label.text = "100/100"
value_label.add_theme_font_size_override("font_size", 14)

health_bar.add_child(bar_container)
bar_container.set_owner(health_bar)
health_bar.add_child(value_label)
value_label.set_owner(health_bar)
```

#### ランタイムコントローラー（GDScript）

```gdscript
# health_bar_controller.gd
extends HBoxContainer

@onready var fill: ColorRect = $BarContainer/Fill
@onready var delayed_fill: ColorRect = $BarContainer/DelayedFill
@onready var value_label: Label = $ValueLabel

@export var delayed_fill_speed: float = 0.5

var max_health: float = 100.0
var current_health: float = 100.0
var delayed_health: float = 100.0

func set_health(health: float, max_hp: float) -> void:
	current_health = clampf(health, 0.0, max_hp)
	max_health = max_hp
	var ratio: float = current_health / max_health
	fill.size.x = fill.get_parent().size.x * ratio
	value_label.text = "%d/%d" % [int(current_health), int(max_health)]
	if delayed_health < current_health:
		delayed_health = current_health
		delayed_fill.size.x = fill.size.x

func _process(delta: float) -> void:
	if delayed_health > current_health:
		delayed_health = move_toward(
			delayed_health, current_health,
			max_health * delayed_fill_speed * delta
		)
		delayed_fill.size.x = (delayed_health / max_health) * delayed_fill.get_parent().size.x
```

---

### 2. スキルクールダウン

クールダウン中は暗転 + 円形マスクで残り時間を表示。

#### ノード構造

```
SkillSlot (Panel)            - 64x64
├── Icon (TextureRect)       - スキルアイコン
├── CooldownOverlay (TextureProgressBar) - 暗転オーバーレイ（放射状）
├── CooldownLabel (Label)    - 残り秒数
└── KeyHint (Label)          - "Q"
```

#### EditorScript実装

```gdscript
var skill_slot = Panel.new()
skill_slot.name = "SkillSlot"
skill_slot.custom_minimum_size = Vector2(64, 64)

var icon = TextureRect.new()
icon.name = "Icon"
icon.set_anchors_preset(Control.PRESET_FULL_RECT)
icon.expand_mode = TextureRect.EXPAND_FIT_WIDTH_PROPORTIONAL
skill_slot.add_child(icon)
icon.set_owner(skill_slot)

# クールダウンオーバーレイ（TextureProgressBar で放射状表示）
var cd_overlay = TextureProgressBar.new()
cd_overlay.name = "CooldownOverlay"
cd_overlay.set_anchors_preset(Control.PRESET_FULL_RECT)
cd_overlay.fill_mode = TextureProgressBar.FILL_CLOCKWISE           # 時計回り
cd_overlay.radial_center_offset = Vector2.ZERO
cd_overlay.value = 0.0
cd_overlay.max_value = 1.0
cd_overlay.tint_progress = Color(0, 0, 0, 0.75)
cd_overlay.mouse_filter = Control.MOUSE_FILTER_IGNORE
skill_slot.add_child(cd_overlay)
cd_overlay.set_owner(skill_slot)

# 残り秒数ラベル（中央）
var cd_label = Label.new()
cd_label.name = "CooldownLabel"
cd_label.set_anchors_preset(Control.PRESET_CENTER)
cd_label.text = ""
cd_label.add_theme_font_size_override("font_size", 18)
cd_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
cd_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
cd_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
skill_slot.add_child(cd_label)
cd_label.set_owner(skill_slot)

# キーヒント（左下）
var key_hint = Label.new()
key_hint.name = "KeyHint"
key_hint.set_anchors_preset(Control.PRESET_BOTTOM_LEFT)
key_hint.text = "Q"
key_hint.add_theme_font_size_override("font_size", 11)
key_hint.mouse_filter = Control.MOUSE_FILTER_IGNORE
skill_slot.add_child(key_hint)
key_hint.set_owner(skill_slot)
```

#### ランタイムコントローラー

```gdscript
# skill_slot_controller.gd
extends Panel

@onready var cd_overlay: TextureProgressBar = $CooldownOverlay
@onready var cd_label: Label = $CooldownLabel

var cooldown_duration: float = 0.0
var cooldown_remaining: float = 0.0

func start_cooldown(duration: float) -> void:
	cooldown_duration = duration
	cooldown_remaining = duration

func _process(delta: float) -> void:
	if cooldown_remaining > 0.0:
		cooldown_remaining -= delta
		var ratio: float = cooldown_remaining / cooldown_duration
		cd_overlay.value = ratio
		if cooldown_remaining > 1.0:
			cd_label.text = str(ceili(cooldown_remaining))
		elif cooldown_remaining > 0.0:
			cd_label.text = "%.1f" % cooldown_remaining
		else:
			cooldown_remaining = 0.0
			cd_overlay.value = 0.0
			cd_label.text = ""
```

---

### 3. インベントリグリッド

レアリティ枠色、スタック数表示対応。

#### ノード構造

```
InventoryPanel (Panel)
└── ScrollContainer
    └── InventoryGrid (GridContainer)  - columns=6
        └── ItemSlot (Panel) × N
            ├── Background (ColorRect)  - レアリティ枠
            ├── Icon (TextureRect)
            ├── StackCount (Label)      - "x99"（右下）
            └── SelectionHighlight (ColorRect) - 選択時ハイライト
```

#### EditorScript実装

```gdscript
var inventory_panel = Panel.new()
inventory_panel.name = "InventoryPanel"
inventory_panel.custom_minimum_size = Vector2(420, 300)

var scroll = ScrollContainer.new()
scroll.name = "ScrollContainer"
scroll.set_anchors_preset(Control.PRESET_FULL_RECT)
inventory_panel.add_child(scroll)
scroll.set_owner(inventory_panel)

var grid = GridContainer.new()
grid.name = "InventoryGrid"
grid.columns = 6
grid.add_theme_constant_override("h_separation", 4)
grid.add_theme_constant_override("v_separation", 4)
scroll.add_child(grid)
grid.set_owner(inventory_panel)

# アイテムスロット作成（1個）
var slot = Panel.new()
slot.name = "ItemSlot"
slot.custom_minimum_size = Vector2(64, 64)

var slot_bg = ColorRect.new()
slot_bg.name = "Background"
slot_bg.set_anchors_preset(Control.PRESET_FULL_RECT)
slot_bg.color = Color(0.3, 0.3, 0.3, 1.0)  # コモン（デフォルト）
slot.add_child(slot_bg)
slot_bg.set_owner(slot)

var slot_icon = TextureRect.new()
slot_icon.name = "Icon"
slot_icon.set_anchors_preset(Control.PRESET_FULL_RECT)
slot_icon.expand_mode = TextureRect.EXPAND_FIT_WIDTH_PROPORTIONAL
slot_icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
slot.add_child(slot_icon)
slot_icon.set_owner(slot)

var stack_label = Label.new()
stack_label.name = "StackCount"
stack_label.set_anchors_preset(Control.PRESET_BOTTOM_RIGHT)
stack_label.text = ""
stack_label.add_theme_font_size_override("font_size", 11)
stack_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
stack_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
slot.add_child(stack_label)
stack_label.set_owner(slot)

var highlight = ColorRect.new()
highlight.name = "SelectionHighlight"
highlight.set_anchors_preset(Control.PRESET_FULL_RECT)
highlight.color = Color(1, 1, 1, 0.0)  # 通常時は非表示
highlight.mouse_filter = Control.MOUSE_FILTER_IGNORE
slot.add_child(highlight)
highlight.set_owner(slot)

grid.add_child(slot)
slot.set_owner(inventory_panel)
```

#### レアリティカラー定義（GDScript）

```gdscript
# item_rarity.gd
enum ItemRarity { COMMON, UNCOMMON, RARE, EPIC, LEGENDARY }

const RARITY_COLORS: Dictionary[int, Color] = {
	ItemRarity.COMMON:    Color(0.6, 0.6, 0.6),
	ItemRarity.UNCOMMON:  Color(0.2, 0.8, 0.2),
	ItemRarity.RARE:      Color(0.2, 0.4, 0.9),
	ItemRarity.EPIC:      Color(0.6, 0.2, 0.9),
	ItemRarity.LEGENDARY: Color(1.0, 0.5, 0.0),
}

static func get_color(rarity: ItemRarity) -> Color:
	return RARITY_COLORS.get(rarity, Color.WHITE)
```

---

### 4. ダメージ数値（Floating Text）

ダメージを受けた位置から数値が浮き上がってフェードアウト。
Godot では **Label を Node2D（またはNode3D）配下に置き**、Tween でアニメーション。

#### EditorScript実装（プレハブ的な子シーン作成）

```gdscript
# DamageNumber プレハブ作成
var dn_root = Node2D.new()
dn_root.name = "DamageNumber"

var dn_label = Label.new()
dn_label.name = "Label"
dn_label.text = "999"
dn_label.add_theme_font_size_override("font_size", 24)
dn_label.add_theme_color_override("font_color", Color(1, 0.2, 0.2))
dn_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
dn_root.add_child(dn_label)
dn_label.set_owner(dn_root)
```

#### ランタイムコントローラー

```gdscript
# damage_number.gd
extends Node2D

@onready var label: Label = $Label

func setup(damage: int, is_critical: bool = false) -> void:
	label.text = str(damage)
	if is_critical:
		label.add_theme_font_size_override("font_size", 36)
		label.add_theme_color_override("font_color", Color(1, 0.8, 0))

	var tween: Tween = create_tween().set_parallel()
	var rand_x: float = randf_range(-20.0, 20.0)
	tween.tween_property(self, "position",
		position + Vector2(rand_x, -80), 1.2
	).set_ease(Tween.EASE_OUT)
	tween.tween_property(label, "modulate:a", 0.0, 1.2
	).set_delay(0.5)
	tween.chain().tween_callback(queue_free)

# 使用方法（ランタイムから呼び出し）:
# var dn = preload("res://ui/damage_number.tscn").instantiate()
# add_child(dn)
# dn.global_position = enemy.global_position + Vector2(0, -50)
# dn.setup(damage_amount, is_crit)
```

---

### 5. ミニマップ

SubViewport + TextureRect を使った俯瞰ミニマップ。

#### ノード構造

```
MinimapContainer (Panel)       - 150x150
├── SubViewportContainer (SubViewportContainer)
│   └── SubViewport (SubViewport) - ミニマップ用カメラ
│       └── Camera2D (Camera2D)   - 上から見下ろしカメラ
├── PlayerIcon (TextureRect)   - プレイヤーアイコン（中央固定）
└── Border (Panel)             - 枠装飾
```

#### EditorScript実装

```gdscript
var minimap_container = Panel.new()
minimap_container.name = "MinimapContainer"
minimap_container.custom_minimum_size = Vector2(150, 150)
minimap_container.set_anchors_preset(Control.PRESET_TOP_RIGHT)
minimap_container.position = Vector2(-170, 20)

var svc = SubViewportContainer.new()
svc.name = "SubViewportContainer"
svc.set_anchors_preset(Control.PRESET_FULL_RECT)
svc.stretch = true
minimap_container.add_child(svc)
svc.set_owner(minimap_container)

var sv = SubViewport.new()
sv.name = "SubViewport"
sv.size = Vector2i(150, 150)
sv.render_target_update_mode = SubViewport.UPDATE_ALWAYS
svc.add_child(sv)
sv.set_owner(minimap_container)

var player_icon = TextureRect.new()
player_icon.name = "PlayerIcon"
player_icon.set_anchors_preset(Control.PRESET_CENTER)
player_icon.custom_minimum_size = Vector2(16, 16)
player_icon.mouse_filter = Control.MOUSE_FILTER_IGNORE
minimap_container.add_child(player_icon)
player_icon.set_owner(minimap_container)

var border = Panel.new()
border.name = "Border"
border.set_anchors_preset(Control.PRESET_FULL_RECT)
border.mouse_filter = Control.MOUSE_FILTER_IGNORE
minimap_container.add_child(border)
border.set_owner(minimap_container)
```

---

### 6. ダイアログシステム

RPG風の会話ウィンドウ。タイプライター効果付き。

#### ノード構造

```
DialogPanel (Panel)
├── SpeakerName (Label)            - 話者名
├── Portrait (TextureRect)         - 話者アイコン（80x80）
├── DialogText (RichTextLabel)     - 会話テキスト（タイプライター）
├── ChoicesContainer (VBoxContainer) - 選択肢
│   └── ChoiceButton (Button) × N
└── ContinueIndicator (Label)      - "▼" 次へ矢印
```

#### EditorScript実装

```gdscript
var dialog_panel = Panel.new()
dialog_panel.name = "DialogPanel"
# 下部ストレッチ（画面幅全体 × 30%）
dialog_panel.anchor_left = 0.0
dialog_panel.anchor_right = 1.0
dialog_panel.anchor_top = 0.7
dialog_panel.anchor_bottom = 1.0
dialog_panel.offset_left = 0
dialog_panel.offset_right = 0
dialog_panel.offset_top = 0
dialog_panel.offset_bottom = 0

var speaker_label = Label.new()
speaker_label.name = "SpeakerName"
speaker_label.text = "村人A"
speaker_label.add_theme_font_size_override("font_size", 20)
speaker_label.add_theme_color_override("font_color", Color(1, 0.9, 0.4))
dialog_panel.add_child(speaker_label)
speaker_label.set_owner(dialog_panel)

var portrait = TextureRect.new()
portrait.name = "Portrait"
portrait.custom_minimum_size = Vector2(80, 80)
portrait.expand_mode = TextureRect.EXPAND_FIT_WIDTH_PROPORTIONAL
portrait.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
dialog_panel.add_child(portrait)
portrait.set_owner(dialog_panel)

var dialog_text = RichTextLabel.new()
dialog_text.name = "DialogText"
dialog_text.bbcode_enabled = true
dialog_text.fit_content = true
dialog_text.text = "こんにちは、旅人さん。"
dialog_panel.add_child(dialog_text)
dialog_text.set_owner(dialog_panel)

var choices = VBoxContainer.new()
choices.name = "ChoicesContainer"
choices.add_theme_constant_override("separation", 8)
dialog_panel.add_child(choices)
choices.set_owner(dialog_panel)

var continue_indicator = Label.new()
continue_indicator.name = "ContinueIndicator"
continue_indicator.text = "▼"
continue_indicator.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
dialog_panel.add_child(continue_indicator)
continue_indicator.set_owner(dialog_panel)
```

#### タイプライター効果（GDScript）

```gdscript
# dialog_controller.gd
extends Panel

@onready var dialog_text: RichTextLabel = $DialogText
@onready var continue_indicator: Label = $ContinueIndicator
@onready var choices_container: VBoxContainer = $ChoicesContainer

@export var characters_per_second: float = 30.0

var full_text: String = ""
var is_typing: bool = false

func show_dialog(text: String) -> void:
	full_text = text
	dialog_text.text = ""
	continue_indicator.visible = false
	is_typing = true
	_type_next_char(0)

func _type_next_char(index: int) -> void:
	if index >= full_text.length():
		is_typing = false
		continue_indicator.visible = true
		return
	dialog_text.text += full_text[index]
	get_tree().create_timer(1.0 / characters_per_second).timeout.connect(
		_type_next_char.bind(index + 1), CONNECT_ONE_SHOT
	)

func skip_typing() -> void:
	if is_typing:
		is_typing = false
		dialog_text.text = full_text
		continue_indicator.visible = true

func _gui_input(event: InputEvent) -> void:
	if event.is_action_pressed("ui_accept"):
		if is_typing:
			skip_typing()
		else:
			_on_dialog_complete()

func _on_dialog_complete() -> void:
	pass  # 次のダイアログへ、またはパネルを非表示
```

---

## アンカー設定リファレンス

GodotのControlノードはプロパティで直接アンカーを設定する。

```gdscript
# 方法1: プリセット（最もシンプル）
control.set_anchors_preset(Control.PRESET_TOP_LEFT)     # 左上
control.set_anchors_preset(Control.PRESET_TOP_RIGHT)    # 右上
control.set_anchors_preset(Control.PRESET_BOTTOM_LEFT)  # 左下
control.set_anchors_preset(Control.PRESET_BOTTOM_RIGHT) # 右下
control.set_anchors_preset(Control.PRESET_CENTER)       # 中央
control.set_anchors_preset(Control.PRESET_FULL_RECT)    # 全画面ストレッチ
control.set_anchors_preset(Control.PRESET_TOP_WIDE)     # 上部ストレッチ
control.set_anchors_preset(Control.PRESET_BOTTOM_WIDE)  # 下部ストレッチ
control.set_anchors_preset(Control.PRESET_LEFT_WIDE)    # 左側ストレッチ
control.set_anchors_preset(Control.PRESET_CENTER_BOTTOM)# 下部中央

# 方法2: 個別プロパティ（細かい制御）
control.anchor_left   = 0.0  # 左アンカー（0〜1）
control.anchor_right  = 1.0  # 右アンカー（0〜1）
control.anchor_top    = 0.0  # 上アンカー（0〜1）
control.anchor_bottom = 1.0  # 下アンカー（0〜1）
control.offset_left   = 0    # 左マージン（px）
control.offset_right  = 0    # 右マージン（px）
control.offset_top    = 0    # 上マージン（px）
control.offset_bottom = 0    # 下マージン（px）
```

---

## レスポンシブデザイン

### プロジェクト設定（Stretch Mode）

```gdscript
# Project Settings > Display > Window > Stretch
# canvas_items: CanvasItem全体をスケール（最もシンプル）
# viewport: ビューポート解像度固定（ピクセルアート向け）
# disabled: スケールなし

# ランタイムで基準解像度に対するスケールを取得
var viewport_size: Vector2 = get_viewport().get_visible_rect().size
var base_size: Vector2 = Vector2(1920, 1080)
var scale_factor: float = minf(viewport_size.x / base_size.x, viewport_size.y / base_size.y)
```

### Safe Area対応（ノッチ対応）

```gdscript
# safe_area_handler.gd
extends MarginContainer

func _ready() -> void:
	_apply_safe_area()
	get_viewport().size_changed.connect(_apply_safe_area)

func _apply_safe_area() -> void:
	var safe_area: Rect2i = DisplayServer.get_display_safe_area()
	var viewport_size: Vector2i = DisplayServer.window_get_size()

	add_theme_constant_override("margin_left",   safe_area.position.x)
	add_theme_constant_override("margin_top",    safe_area.position.y)
	add_theme_constant_override("margin_right",  viewport_size.x - safe_area.end.x)
	add_theme_constant_override("margin_bottom", viewport_size.y - safe_area.end.y)
```

---

## Layout Containers（自動レイアウト）

```gdscript
# HBoxContainer — 横並び
var hbox = HBoxContainer.new()
hbox.add_theme_constant_override("separation", 10)

# VBoxContainer — 縦並び
var vbox = VBoxContainer.new()
vbox.add_theme_constant_override("separation", 5)
vbox.alignment = BoxContainer.ALIGNMENT_CENTER

# GridContainer — グリッド
var grid = GridContainer.new()
grid.columns = 4
grid.add_theme_constant_override("h_separation", 10)
grid.add_theme_constant_override("v_separation", 10)

# MarginContainer — マージン付き
var margin = MarginContainer.new()
margin.add_theme_constant_override("margin_left", 16)
margin.add_theme_constant_override("margin_right", 16)
margin.add_theme_constant_override("margin_top", 16)
margin.add_theme_constant_override("margin_bottom", 16)

# ScrollContainer — スクロール
var scroll = ScrollContainer.new()
scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
# scroll.vertical_scroll_mode は SCROLL_MODE_AUTO（デフォルト）

# PanelContainer — StyleBoxでスタイリング
var panel = PanelContainer.new()
var style = StyleBoxFlat.new()
style.bg_color = Color(0, 0, 0, 0.8)
style.corner_radius_top_left = 8
style.corner_radius_top_right = 8
style.corner_radius_bottom_left = 8
style.corner_radius_bottom_right = 8
panel.add_theme_stylebox_override("panel", style)
```

---

## スタイリング（StyleBox）

UnityのUI Materialに相当するのがGodotの**StyleBox**。

```gdscript
# 角丸パネル（ダイアログ背景）
var style = StyleBoxFlat.new()
style.bg_color = Color(0.05, 0.05, 0.1, 0.92)
style.corner_radius_top_left    = 12
style.corner_radius_top_right   = 12
style.corner_radius_bottom_left = 12
style.corner_radius_bottom_right = 12
style.border_color = Color(0.4, 0.4, 0.6, 1.0)
style.border_width_left   = 1
style.border_width_right  = 1
style.border_width_top    = 1
style.border_width_bottom = 1
panel.add_theme_stylebox_override("panel", style)

# ボタン（通常/ホバー/押下）
var btn_normal = StyleBoxFlat.new()
btn_normal.bg_color = Color(0.2, 0.3, 0.5)
btn_normal.corner_radius_top_left = 6
btn_normal.corner_radius_top_right = 6
btn_normal.corner_radius_bottom_left = 6
btn_normal.corner_radius_bottom_right = 6

var btn_hover = StyleBoxFlat.new()
btn_hover.bg_color = Color(0.3, 0.45, 0.7)
btn_hover.corner_radius_top_left = 6
btn_hover.corner_radius_top_right = 6
btn_hover.corner_radius_bottom_left = 6
btn_hover.corner_radius_bottom_right = 6

var btn_pressed = StyleBoxFlat.new()
btn_pressed.bg_color = Color(0.15, 0.22, 0.38)
btn_pressed.corner_radius_top_left = 6
btn_pressed.corner_radius_top_right = 6
btn_pressed.corner_radius_bottom_left = 6
btn_pressed.corner_radius_bottom_right = 6

button.add_theme_stylebox_override("normal",  btn_normal)
button.add_theme_stylebox_override("hover",   btn_hover)
button.add_theme_stylebox_override("pressed", btn_pressed)
```

---

## CanvasLayer 管理

```gdscript
# 推奨 CanvasLayer 構成
# Layer  1: ゲームワールド（2D）
# Layer  5: ゲームUI（HUD）
# Layer 10: システムUI（メニュー、ポーズ）
# Layer 50: ローディング画面
# Layer 100: デバッグオーバーレイ、全画面エフェクト

var hud_layer = CanvasLayer.new()
hud_layer.layer = 5

var menu_layer = CanvasLayer.new()
menu_layer.layer = 10
menu_layer.visible = false  # ポーズ時に表示
```

---

## Common Mistakes（よくあるミス）

### 1. アンカー未設定でUI位置がずれる

```gdscript
# NG: 固定位置のみ設定（画面サイズ変更でずれる）
control.position = Vector2(1820, 20)  # 右上のつもりが…

# OK: アンカープリセット + オフセット
control.set_anchors_preset(Control.PRESET_TOP_RIGHT)
control.offset_left   = -200  # 右端から200px左
control.offset_top    =  20   # 上端から20px下
control.offset_right  = -20
control.offset_bottom =  60
```

### 2. CanvasLayerなしでゲームUIを実装

```gdscript
# NG: UIをゲームNodeと同じ階層に置くとカメラ移動で追従してしまう
game_node.add_child(hud_panel)

# OK: CanvasLayerに配置（カメラと独立）
canvas_layer.add_child(hud_panel)
```

### 3. Control.size を _ready() で直接設定

```gdscript
# NG: レイアウト前に size を設定しても後で上書きされる
func _ready():
    self.size = Vector2(200, 50)  # 効果なし（LayoutEngine が後で書き換える）

# OK: call_deferred を使うかカスタムサイズを minimum_size で設定
func _ready():
    custom_minimum_size = Vector2(200, 50)
    # または
    set_deferred("size", Vector2(200, 50))
```

### 4. TextureProgressBar の fill_mode 設定ミス

```gdscript
# クールダウン時計回り表示
cd_overlay.fill_mode = TextureProgressBar.FILL_CLOCKWISE
# 注意: FILL_CLOCKWISE は value が 0 = 空、1 = 満タン
# クールダウン残量 = remaining / total なので value の意味に注意
```

### 5. RichTextLabel の bbcode_enabled 忘れ

```gdscript
# NG: bbcode_enabled = false だと [color=red]text[/color] がそのまま表示される
var rtl = RichTextLabel.new()
rtl.text = "[color=red]ダメージ[/color]"  # タグが文字として表示される

# OK:
rtl.bbcode_enabled = true
rtl.text = "[color=red]ダメージ[/color]"  # 赤字で表示
```
