# Glossary: what a message says -> the engine terms the knowledge base is indexed by.
#
# Gotchas and API notes are selected by English class and method names. A
# message written in another language ("给角色加上跳跃") names none of them, so
# nothing would be selected. Each line maps one or more terms, separated by
# commas, to the names they stand for; when a term occurs in the message, the
# names are added to the text the selection runs on. The message itself is sent
# to the model unchanged.
#
#   term, another term => EngineName, method_name, topic
#
# Lines starting with # are comments. Matching ignores case.

# Movement and physics
跳跃, 跳起, 二段跳 => CharacterBody2D, CharacterBody3D, move_and_slide, is_on_floor, velocity, jump
移动, 行走, 走路, 奔跑 => CharacterBody2D, CharacterBody3D, move_and_slide, velocity, _physics_process
角色, 玩家, 主角 => CharacterBody2D, CharacterBody3D, move_and_slide
重力, 下落, 掉落 => velocity, _physics_process, is_on_floor, gravity
碰撞, 碰撞体, 碰撞形状 => CollisionShape2D, CollisionShape3D, collision_layer, collision_mask, collision
碰撞层, 碰撞掩码, 图层 => collision_layer, collision_mask
刚体 => RigidBody2D, RigidBody3D, apply_impulse, physics
区域, 触发器, 检测范围 => Area2D, Area3D, body_entered, area_entered
射线, 射线检测 => RayCast2D, RayCast3D, PhysicsRayQueryParameters2D, PhysicsRayQueryParameters3D, intersect_ray
物理 => _physics_process, physics, PhysicsServer2D, PhysicsServer3D
子弹, 投射物, 发射 => Area2D, body_entered, queue_free, instantiate
敌人, 怪物 => CharacterBody2D, NavigationAgent2D, add_to_group
拾取, 金币, 道具 => Area2D, body_entered, queue_free

# Scenes, nodes, resources
场景, 切换场景, 换关 => PackedScene, change_scene_to_file, instantiate, SceneTree
实例化, 生成, 预制 => PackedScene, instantiate, add_child
节点 => add_child, owner, get_node, queue_free
分组, 组 => add_to_group, get_nodes_in_group
信号, 连接信号 => signal, connect, emit, Callable
自动加载, 单例, 全局脚本 => autoload, ProjectSettings
资源, 自定义资源 => Resource, ResourceSaver, ResourceLoader, preload
保存, 存档, 读档 => FileAccess, ResourceSaver, JSON, ConfigFile, user
加载 => ResourceLoader, preload, load_threaded_request
定时器, 计时, 倒计时, 冷却 => Timer, create_timer, timeout
随机 => randi, randf, RandomNumberGenerator
导出变量, 检查器 => @export, export

# Animation
动画 => AnimationPlayer, AnimatedSprite2D, AnimationTree, animation
动画树, 状态机 => AnimationTree, AnimationNodeStateMachinePlayback, travel
补间, 缓动, 渐变 => Tween, create_tween, tween_property
帧动画, 精灵动画 => AnimatedSprite2D, SpriteFrames, play
骨骼 => Skeleton3D, Skeleton2D, BoneAttachment3D

# 2D
精灵, 贴图, 图片 => Sprite2D, Texture2D, texture
瓦片, 地图, 瓦片地图, 地形 => TileMapLayer, TileSet, set_cell, TileMap
视差, 背景滚动 => Parallax2D, ParallaxBackground
相机, 摄像机, 镜头 => Camera2D, Camera3D, make_current
绘制, 画线, 自绘 => _draw, draw_line, queue_redraw, CanvasItem
粒子, 特效 => GPUParticles2D, GPUParticles3D, CPUParticles2D, ParticleProcessMaterial, emitting
路径, 跟随路径 => Path2D, PathFollow2D, Curve2D
光照, 灯光, 阴影 => PointLight2D, DirectionalLight3D, OmniLight3D, shadow, Light2D

# 3D
三维, 3D => Node3D, MeshInstance3D, Camera3D
网格, 模型 => MeshInstance3D, Mesh, BoxMesh, import
材质 => StandardMaterial3D, ShaderMaterial, material
环境, 天空, 雾 => WorldEnvironment, Environment, Sky
导航, 寻路, 巡逻 => NavigationAgent2D, NavigationAgent3D, NavigationRegion2D, NavigationRegion3D, NavigationServer2D, navigation
网格地图 => GridMap, MeshLibrary

# UI
界面, 菜单, 主菜单, 暂停菜单 => Control, Button, VBoxContainer, CanvasLayer, UI
按钮 => Button, pressed
标签, 文字, 文本 => Label, RichTextLabel, text
血条, 进度条, 生命值 => ProgressBar, TextureProgressBar, value
布局, 锚点, 对齐, 居中 => Control, set_anchors_preset, anchors, Container, size_flags
容器 => Container, VBoxContainer, HBoxContainer, GridContainer, MarginContainer
主题, 字体, 字号, 样式 => Theme, add_theme_color_override, add_theme_font_size_override, StyleBoxFlat, LabelSettings
输入框 => LineEdit, TextEdit, text_changed
对话框, 弹窗 => AcceptDialog, Popup, Window
列表, 下拉 => ItemList, OptionButton, Tree
计分, 分数, 得分 => Label, text, signal

# Input
输入, 按键, 键盘 => Input, InputMap, is_action_pressed, _unhandled_input, InputEventKey
鼠标, 点击 => InputEventMouseButton, get_global_mouse_position, _input
手柄, 摇杆 => InputEventJoypadButton, get_vector, Input
触摸, 触屏 => InputEventScreenTouch, InputEventScreenDrag
输入映射, 动作 => InputMap, add_action, action_add_event

# Audio, shaders, misc
音效, 音乐, 声音, 音频 => AudioStreamPlayer, AudioStreamPlayer2D, AudioStream, AudioServer, audio
着色器, shader => Shader, ShaderMaterial, shader_type, uniform
视口, 分屏 => Viewport, SubViewport, get_texture
多线程, 线程 => Thread, Mutex, WorkerThreadPool
联机, 多人, 网络 => MultiplayerAPI, MultiplayerSpawner, rpc, ENetMultiplayerPeer
本地化, 翻译, 多语言 => TranslationServer, tr
导出, 打包 => export_presets, export
性能, 卡顿, 优化, 帧率 => Performance, _process, _physics_process, FPS
调试, 报错, 错误 => print, push_error, assert, breakpoint
类型, 类型提示 => GDScript, typed, Array, Dictionary
协程, 等待, 异步 => await, signal, create_timer
脚本, 代码 => GDScript, @export, _ready, extends
