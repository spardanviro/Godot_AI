---
id: particles
order: 40
match: stem
keywords:
  - particle
  - gpuparticle
  - cpuparticle
  - emitter
  - emission
  - 粒子
  - 发射器
---

## Particle System Specialization

### GPUParticles2D Setup
```gdscript
var particles = GPUParticles2D.new()
particles.name = "Sparks"
particles.amount = 50
particles.lifetime = 1.0
particles.emitting = true
var mat = ParticleProcessMaterial.new()
mat.direction = Vector3(0, -1, 0)
mat.spread = 30.0
mat.initial_velocity_min = 50.0
mat.initial_velocity_max = 100.0
mat.gravity = Vector3(0, 98, 0)
mat.scale_min = 0.5
mat.scale_max = 1.5
# Color ramp
var gradient = Gradient.new()
gradient.set_color(0, Color(1, 0.8, 0, 1))
gradient.add_point(0.5, Color(1, 0.3, 0, 1))
gradient.set_color(1, Color(0.3, 0, 0, 0))
var grad_tex = GradientTexture1D.new()
grad_tex.gradient = gradient
mat.color_ramp = grad_tex
particles.process_material = mat
```

### Emission Shapes
```gdscript
# Point (default), Sphere, Box, Ring
mat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_SPHERE
mat.emission_sphere_radius = 20.0
# Box:
mat.emission_shape = ParticleProcessMaterial.EMISSION_SHAPE_BOX
mat.emission_box_extents = Vector3(50, 10, 0)
```

### 3D Particles
Use GPUParticles3D with the same ParticleProcessMaterial. Add a mesh to `draw_pass_1`:
```gdscript
var particles3d = GPUParticles3D.new()
particles3d.draw_pass_1 = SphereMesh.new()  # or QuadMesh
particles3d.process_material = mat
```
