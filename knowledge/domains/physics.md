---
id: physics
order: 60
match: stem
keywords:
  - rigidbody
  - staticbody
  - characterbody
  - collision
  - raycast
  - joint
  - hinge
  - collision_layer
  - collision_mask
  - 物理
  - 碰撞
  - 刚体
  - 射线
---

## Physics System Specialization

### Collision Shape Setup
```gdscript
# 2D: Add collision to a body
var col = CollisionShape2D.new()
col.name = "CollisionShape2D"
var shape = RectangleShape2D.new()
shape.size = Vector2(32, 32)
col.shape = shape
body.add_child(col)
col.owner = scene_root

# 3D: BoxShape3D, SphereShape3D, CapsuleShape3D, CylinderShape3D
var col3d = CollisionShape3D.new()
var box = BoxShape3D.new()
box.size = Vector3(1, 2, 1)
col3d.shape = box
```

### Collision Layers & Masks
```gdscript
# Layers: what this object IS on
# Masks: what this object DETECTS
body.collision_layer = 1  # Layer 1 (bit 0)
body.collision_mask = 2   # Detects layer 2 (bit 1)
# Use set_collision_layer_value / set_collision_mask_value for named layers:
body.set_collision_layer_value(1, true)  # Enable layer 1
body.set_collision_mask_value(2, true)   # Detect layer 2
```

### RayCast (in game scripts)
```gdscript
var ray = RayCast2D.new()
ray.target_position = Vector2(0, 50)  # Cast downward 50px
ray.collision_mask = 1  # Only detect layer 1
ray.enabled = true
# Query: ray.is_colliding(), ray.get_collider(), ray.get_collision_point()
```

### Physics Joints (2D)
```gdscript
var pin = PinJoint2D.new()
pin.node_a = body_a.get_path()
pin.node_b = body_b.get_path()
pin.position = body_a.position  # Pivot point
```

### RigidBody Forces
```gdscript
# In game scripts:
# rigid_body.apply_central_impulse(Vector2(0, -300))  # Jump
# rigid_body.apply_force(Vector2(100, 0))             # Continuous
# rigid_body.linear_velocity = Vector2(0, 0)          # Stop
```
