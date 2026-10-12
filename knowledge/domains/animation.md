---
id: animation
order: 10
match: stem
keywords:
  - animation
  - animationtree
  - animationplayer
  - statemachine
  - blendspace
  - blend_tree
  - blend tree
  - blend space
  - keyframe
  - tween
  - 动画
  - 状态机
  - 混合
  - 关键帧
---

## Animation System Specialization

### AnimationPlayer
```gdscript
var anim_player = AnimationPlayer.new()
anim_player.name = "AnimationPlayer"
parent.add_child(anim_player)
anim_player.owner = scene_root
var anim = Animation.new()
anim.length = 1.0
anim.loop_mode = Animation.LOOP_LINEAR
# Property track: animate a node's property
var track_idx = anim.add_track(Animation.TYPE_VALUE)
anim.track_set_path(track_idx, "Sprite2D:position")
anim.track_insert_key(track_idx, 0.0, Vector2(0, 0))
anim.track_insert_key(track_idx, 1.0, Vector2(100, 0))
var lib = AnimationLibrary.new()
lib.add_animation("move_right", anim)
anim_player.add_animation_library("", lib)
```

### AnimationTree + StateMachine
```gdscript
var tree = AnimationTree.new()
tree.name = "AnimationTree"
# Root is an AnimationNodeStateMachine
var sm = AnimationNodeStateMachine.new()
# Add states
sm.add_node("idle", AnimationNodeAnimation.new())
sm.add_node("run",  AnimationNodeAnimation.new())
sm.add_node("jump", AnimationNodeAnimation.new())
# Set animation names
sm.get_node("idle").animation = "idle"
sm.get_node("run").animation  = "run"
sm.get_node("jump").animation = "jump"
# Add transitions
var t1 = AnimationNodeStateMachineTransition.new()
t1.advance_mode = AnimationNodeStateMachineTransition.ADVANCE_MODE_AUTO
sm.add_transition("idle", "run", t1)
var t2 = AnimationNodeStateMachineTransition.new()
sm.add_transition("run", "idle", t2)
tree.tree_root = sm
tree.anim_player = NodePath("../AnimationPlayer")
parent.add_child(tree)
tree.owner = scene_root
```

### BlendSpace2D
```gdscript
var bs = AnimationNodeBlendSpace2D.new()
var anim_idle = AnimationNodeAnimation.new()
anim_idle.animation = "idle"
bs.add_blend_point(anim_idle, Vector2(0, 0))
var anim_run = AnimationNodeAnimation.new()
anim_run.animation = "run"
bs.add_blend_point(anim_run, Vector2(1, 0))
bs.min_space = Vector2(-1, -1)
bs.max_space = Vector2(1, 1)
```

### Tween (procedural animation)
```gdscript
# In game scripts, NOT in EditorScript:
var tween = create_tween()
tween.tween_property(sprite, "position", Vector2(200, 0), 0.5)
tween.set_trans(Tween.TRANS_QUAD).set_ease(Tween.EASE_OUT)
tween.tween_callback(func(): print("done"))
```
