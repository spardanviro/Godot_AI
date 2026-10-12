---
id: complex_project
order: 110
match: stem
keywords:
  - complete game
  - full game
  - whole game
  - entire game
  - make a game
  - build a game
  - create a game
  - complete project
  - full project
  - whole project
  - rpg
  - platformer
  - shooter
  - racing game
  - tower defense
  - 完整游戏
  - 完整项目
  - 制作游戏
  - 创建游戏
  - 动作游戏
  - 平台游戏
  - 塔防御
---

## Risk-First Decomposition (MANDATORY for full game / complete project requests)

When building a COMPLETE GAME or FULL PROJECT, follow risk-first decomposition:

### Step 1 — Identify High-Risk Components
Before writing any code, identify which features carry the highest implementation risk.
ISOLATE these first (prototype separately before integrating):
- **Procedural generation** (dungeon gen, terrain, L-systems) — algorithm correctness is hard to debug in a full scene
- **Custom animations / animation trees** — StateMachine transitions, BlendSpace2D blending
- **Vehicle / ragdoll / cloth physics** — RigidBody joints, spring arms, wheel colliders
- **Custom shaders** — vertex deformation, screen-space effects, geometry shaders
- **Runtime geometry** — Mesh built from SurfaceTool/ArrayMesh, MeshDataTool
- **Dynamic navigation** — runtime navmesh baking, NavigationAgent avoidance
- **Complex camera rigs** — spring arm + lerp + look-at chains (notorious for frame-1 origin glitch)

NEVER isolate these (they are standard, low-risk):
- CharacterBody2D/3D movement with move_and_slide()
- TileMapLayer placement and queries
- NavigationAgent on a static navmesh
- Standard UI (Control containers, Labels, Buttons)
- Spawning / pooling via instantiate()
- Timer-based events

### Step 2 — Build in Order
1. **Core loop first**: movement, collision, camera — verify the scene runs without errors
2. **Content systems**: enemies, items, levels — build on top of working core
3. **Polish**: particles, shaders, sound, UI — add last

### Step 3 — Verification Criteria
Before declaring a feature 'done', verify:
- [ ] No parse errors (script loads without error)
- [ ] No runtime errors in first 30 seconds of play
- [ ] Camera starts at correct position (no origin-swoop on frame 1)
- [ ] Physics collision works (player lands on floor, doesn't fall through)
- [ ] Input actions are registered in InputMap before use

### Common Full-Game Architecture
```
res://
├── scenes/
│   ├── game/        game.tscn (root: Node), game.gd (game loop)
│   ├── player/      player.tscn, player_controller.gd
│   ├── enemies/     enemy_base.tscn, enemy_base.gd
│   ├── ui/          hud.tscn, hud.gd, main_menu.tscn
│   └── levels/      level_01.tscn
├── scripts/
│   └── autoloads/   game_manager.gd, audio_manager.gd
├── resources/
│   └── items/       sword.tres, potion.tres
└── assets/
    ├── textures/
    └── audio/
```

### Signal Architecture for Multi-Script Games
Root node coordinates — children emit signals upward:
- Player emits: `player_died`, `score_changed(delta:int)`, `health_changed(hp:int)`
- Enemy emits: `enemy_died(position:Vector3)`, `damage_dealt(amount:int)`
- GameManager listens to all, updates HUD, triggers level transitions
- NEVER have enemies directly reference the Player node — use groups or signals

