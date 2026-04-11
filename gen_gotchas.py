"""
Generate ai_gotchas_index.cpp from ai_system_prompt.cpp.

Extracts all 401 gotcha blocks and auto-generates keyword sets from headings,
then writes the full C++ source file.
"""
import re
import sys

# ── helpers ──────────────────────────────────────────────────────────────────

def unescape_c(raw: str) -> str:
    result = []
    j = 0
    BS = chr(92)
    while j < len(raw):
        if raw[j] == BS and j + 1 < len(raw):
            nc = raw[j+1]
            if nc == 'n':
                result.append('\n'); j += 2
            elif nc == '"':
                result.append('"'); j += 2
            elif nc == BS:
                result.append(BS); j += 2
            else:
                result.append(raw[j]); j += 1
        else:
            result.append(raw[j]); j += 1
    return ''.join(result)


def escape_cpp(s: str) -> str:
    """Escape a Python string for use as a C++ raw string content."""
    return s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n')


# ── topic-word → keywords mapping ────────────────────────────────────────────
# If a word appears in the heading, add all these keywords.
TOPIC_MAP = {
    # movement / physics
    'move_and_slide': ['CharacterBody2D', 'CharacterBody3D', 'move_and_slide', 'CharacterBody'],
    'move_and_collide': ['CharacterBody2D', 'CharacterBody3D', 'move_and_collide', 'CharacterBody'],
    'velocity': ['CharacterBody2D', 'CharacterBody3D', 'velocity', 'CharacterBody'],
    'gravity': ['gravity', 'physics', 'CharacterBody2D', 'CharacterBody3D'],
    'collision': ['collision', 'physics'],
    'physics': ['physics', 'PhysicsBody2D', 'PhysicsBody3D', 'RigidBody2D', 'RigidBody3D'],
    'rigidbody': ['RigidBody2D', 'RigidBody3D', 'RigidBody', 'physics'],
    'kinematic': ['CharacterBody2D', 'CharacterBody3D', 'CharacterBody'],
    'staticbody': ['StaticBody2D', 'StaticBody3D', 'StaticBody'],
    'area': ['Area2D', 'Area3D', 'body_entered', 'area_entered'],
    'area2d': ['Area2D', 'area_entered', 'body_entered'],
    'area3d': ['Area3D', 'area_entered', 'body_entered'],
    'characterbody': ['CharacterBody2D', 'CharacterBody3D', 'CharacterBody', 'move_and_slide'],

    # navigation / pathfinding
    'navigation': ['NavigationAgent2D', 'NavigationAgent3D', 'NavigationAgent', 'navigation', 'pathfinding'],
    'navigationagent': ['NavigationAgent2D', 'NavigationAgent3D', 'NavigationAgent', 'navigation'],
    'pathfinding': ['NavigationAgent2D', 'NavigationAgent3D', 'pathfinding', 'navigation'],
    'navigationregion': ['NavigationRegion2D', 'NavigationRegion3D', 'NavigationRegion', 'navigation'],
    'navmesh': ['NavigationMesh', 'NavigationRegion3D', 'navmesh', 'navigation'],
    'rvo': ['NavigationAgent2D', 'NavigationAgent3D', 'avoidance', 'RVO'],
    'avoidance': ['NavigationAgent2D', 'NavigationAgent3D', 'avoidance'],

    # signals / nodes
    'signal': ['signal', 'connect', 'emit'],
    'connect': ['signal', 'connect'],
    'await': ['await', 'signal', 'coroutine'],
    'scene': ['scene', 'PackedScene', 'SceneTree'],
    'instanc': ['PackedScene', 'instantiate', 'scene'],
    'node': ['Node', 'add_child', 'get_node'],
    'get_node': ['get_node', 'Node', '$'],
    'add_child': ['add_child', 'Node', 'scene'],
    'queue_free': ['queue_free', 'Node'],
    'owner': ['owner', 'Node', 'scene'],
    '_ready': ['_ready', 'Node', 'scene'],
    '_process': ['_process', 'Node', 'delta'],
    '_physics_process': ['_physics_process', 'Node', 'physics', 'delta'],

    # resources
    'resource': ['Resource', 'load', 'preload'],
    'texture': ['Texture2D', 'Texture', 'texture'],
    'material': ['Material', 'ShaderMaterial', 'material'],
    'mesh': ['Mesh', 'MeshInstance3D', 'mesh'],
    'audiostreamplayer': ['AudioStreamPlayer', 'AudioStreamPlayer2D', 'AudioStreamPlayer3D', 'audio'],
    'audio': ['AudioStreamPlayer', 'audio'],
    'animationplayer': ['AnimationPlayer', 'animation'],
    'animation': ['AnimationPlayer', 'AnimationTree', 'animation'],
    'animationtree': ['AnimationTree', 'AnimationPlayer', 'animation'],
    'sprite': ['Sprite2D', 'Sprite3D', 'AnimatedSprite2D', 'sprite'],
    'spriteframes': ['SpriteFrames', 'AnimatedSprite2D', 'sprite'],
    'tilemap': ['TileMap', 'TileSet', 'tile'],
    'tileset': ['TileSet', 'TileMap', 'tile'],

    # shaders / rendering
    'shader': ['Shader', 'ShaderMaterial', 'shader', 'GLSL'],
    'shadermaterial': ['ShaderMaterial', 'shader'],
    'uniform': ['shader', 'ShaderMaterial', 'uniform'],
    'render': ['rendering', 'Viewport', 'SubViewport'],
    'viewport': ['Viewport', 'SubViewport', 'rendering'],
    'camera': ['Camera2D', 'Camera3D', 'Camera'],
    'camera2d': ['Camera2D', 'camera'],
    'camera3d': ['Camera3D', 'camera'],
    'canvas': ['CanvasLayer', 'CanvasItem', 'canvas'],
    'canvaslayer': ['CanvasLayer', 'canvas'],
    'light': ['Light2D', 'DirectionalLight3D', 'OmniLight3D', 'SpotLight3D', 'light'],
    'environment': ['Environment', 'WorldEnvironment', 'rendering'],

    # UI
    'control': ['Control', 'UI', 'theme'],
    'theme': ['Theme', 'Control', 'UI'],
    'label': ['Label', 'RichTextLabel', 'UI'],
    'button': ['Button', 'UI', 'pressed'],
    'richtextlabel': ['RichTextLabel', 'bbcode', 'UI'],
    'lineedit': ['LineEdit', 'UI', 'text'],
    'textedit': ['TextEdit', 'UI', 'text'],
    'container': ['Container', 'VBoxContainer', 'HBoxContainer', 'UI'],
    'popup': ['Popup', 'PopupMenu', 'UI'],
    'tree': ['Tree', 'TreeItem', 'UI'],
    'itemlist': ['ItemList', 'UI'],
    'scrollcontainer': ['ScrollContainer', 'UI'],
    'spinbox': ['SpinBox', 'UI'],

    # input
    'input': ['Input', 'InputEvent', 'input'],
    'inputevent': ['InputEvent', 'input'],
    'action': ['InputMap', 'input', 'action'],
    'inputmap': ['InputMap', 'input'],
    'mouse': ['mouse', 'Input', 'InputEvent'],
    'keyboard': ['keyboard', 'Input', 'InputEvent'],
    'joypad': ['joypad', 'controller', 'Input'],

    # scripting
    'gdscript': ['GDScript', 'gdscript'],
    'static': ['static', 'GDScript'],
    'export': ['export', 'EditorInspector', 'property'],
    'tool': ['@tool', 'EditorPlugin'],
    'autoload': ['autoload', 'singleton'],
    'singleton': ['autoload', 'singleton'],
    'setget': ['setter', 'getter', 'property'],
    'property': ['@export', 'property', 'setter'],
    'array': ['Array', 'PackedArray', 'GDScript'],
    'dictionary': ['Dictionary', 'GDScript'],
    'typed': ['typed', 'Array', 'GDScript'],
    'lambda': ['lambda', 'Callable', 'GDScript'],
    'callable': ['Callable', 'signal', 'connect'],
    'variant': ['Variant', 'GDScript'],
    'int': ['int', 'float', 'GDScript'],
    'float': ['float', 'int', 'GDScript'],
    'string': ['String', 'StringName', 'GDScript'],
    'stringname': ['StringName', 'String'],
    'enum': ['enum', 'GDScript'],
    'class_name': ['class_name', 'GDScript'],
    'inner class': ['inner class', 'GDScript'],
    'coroutine': ['await', 'coroutine', 'GDScript'],
    'thread': ['Thread', 'Mutex', 'threading'],
    'mutex': ['Mutex', 'Thread', 'threading'],

    # 3D
    '3d': ['Node3D', '3D'],
    'transform': ['Transform3D', 'Transform2D', 'Node3D'],
    'basis': ['Basis', 'Transform3D', '3D'],
    'quaternion': ['Quaternion', 'rotation', '3D'],
    'rotation': ['rotation', 'Transform3D', 'Node3D'],
    'skeleton': ['Skeleton3D', 'Bone', 'animation'],
    'bone': ['Skeleton3D', 'Bone', 'animation'],
    'ik': ['SkeletonIK3D', 'FABRIK', 'Skeleton3D'],
    'csg': ['CSGShape3D', 'CSG', '3D'],
    'gridmap': ['GridMap', '3D'],

    # 2D
    '2d': ['Node2D', '2D'],
    'tilemaplayer': ['TileMapLayer', 'TileMap', 'tile'],

    # files / IO
    'fileaccess': ['FileAccess', 'file', 'IO'],
    'diraccess': ['DirAccess', 'directory', 'IO'],
    'json': ['JSON', 'serialization'],
    'config': ['ConfigFile', 'settings'],
    'configfile': ['ConfigFile', 'settings'],
    'projectsettings': ['ProjectSettings', 'settings'],
    'editorsettings': ['EditorSettings', 'editor'],

    # networking
    'http': ['HTTPRequest', 'network'],
    'httprequest': ['HTTPRequest', 'network'],
    'websocket': ['WebSocket', 'network'],
    'multiplayer': ['MultiplayerAPI', 'RPC', 'network'],
    'rpc': ['RPC', 'MultiplayerAPI', 'network'],

    # export / import
    'export_var': ['@export', 'property', 'EditorInspector'],
    'blender': ['blender', 'import', '.blend', 'GLTF'],
    'gltf': ['GLTF', 'import', 'blender'],
    'import': ['import', 'resource'],
    'fbx': ['FBX', 'import'],

    # editor
    'editorplugin': ['EditorPlugin', 'editor'],
    'editorscript': ['EditorScript', 'editor', '@tool'],
    'editorinterface': ['EditorInterface', 'editor'],

    # misc
    'delta': ['delta', '_process', '_physics_process'],
    'fps': ['FPS', 'performance', 'delta'],
    'performance': ['performance', 'optimization'],
    'debug': ['debug', 'print', 'assert'],
    'print': ['print', 'debug'],
    'assert': ['assert', 'debug'],
    'warning': ['warning', 'GDScript'],
    'error': ['error', 'ERR_'],
    'global': ['global', 'autoload', 'singleton'],
    'tween': ['Tween', 'animation', 'interpolation'],
    'timer': ['Timer', 'wait_time'],
    'raycast': ['RayCast2D', 'RayCast3D', 'RayCast', 'physics'],
    'shapecasting': ['ShapeCast2D', 'ShapeCast3D', 'physics'],
    'collisionshape': ['CollisionShape2D', 'CollisionShape3D', 'CollisionShape', 'collision'],
    'collisionlayer': ['collision_layer', 'collision_mask', 'CollisionObject'],
    'layer': ['collision_layer', 'collision_mask'],
    'mask': ['collision_mask', 'collision_layer'],
    'pathfollow': ['PathFollow2D', 'PathFollow3D', 'Path2D', 'Path3D'],
    'path': ['Path2D', 'Path3D', 'PathFollow2D', 'PathFollow3D'],
    'particles': ['GPUParticles2D', 'GPUParticles3D', 'CPUParticles2D', 'particles'],
    'cpuparticles': ['CPUParticles2D', 'CPUParticles3D', 'particles'],
    'gpuparticles': ['GPUParticles2D', 'GPUParticles3D', 'particles'],
    'softbody': ['SoftBody3D', 'physics'],
    'springarm': ['SpringArm3D', 'camera', '3D'],
    'xr': ['XR', 'XRInterface', 'VR', 'AR'],
    'vr': ['XR', 'VR', 'XRInterface'],
    'gdextension': ['GDExtension', 'C++'],
    'scons': ['SCons', 'build'],
}

# Words that are Godot class names (PascalCase) — extract automatically
PASCAL_RE = re.compile(r'\b([A-Z][a-zA-Z0-9]+(?:[A-Z][a-zA-Z0-9]*)*)\b')

def keywords_for_heading(heading: str) -> list:
    """Auto-extract keywords from a gotcha heading."""
    seen = set()
    result = []

    def add(kw):
        if kw and kw not in seen:
            seen.add(kw)
            result.append(kw)

    h_lower = heading.lower()

    # 1. PascalCase words → likely class names
    for m in PASCAL_RE.finditer(heading):
        w = m.group(1)
        # Skip all-caps or very short (like "OK", "ID")
        if w == w.upper() and len(w) <= 3:
            continue
        add(w)

    # 2. Topic words from TOPIC_MAP
    for topic, kws in TOPIC_MAP.items():
        if topic in h_lower:
            for k in kws:
                add(k)

    # If we found nothing useful, add a generic fallback
    if not result:
        # Try to grab the first meaningful word
        words = re.findall(r'[A-Za-z_][A-Za-z0-9_]*', heading[4:])  # skip ###
        for w in words[:3]:
            if len(w) > 3:
                add(w.lower())

    return result

# ── extract blocks ────────────────────────────────────────────────────────────

with open('ai_system_prompt.cpp', 'r', encoding='utf-8') as f:
    lines = f.readlines()

start = 1144
end = 3418

blocks = []
current_heading = None
current_lines = []

for i in range(start, end):
    line = lines[i].rstrip('\n')
    m = re.match(r'\s*p \+= "(.*?)";$', line)
    if not m:
        continue
    raw = m.group(1)
    content = unescape_c(raw)

    if content.startswith('### '):
        if current_heading is not None:
            blocks.append((current_heading, ''.join(current_lines)))
        current_heading = content.rstrip('\n')
        current_lines = [content]
    elif current_heading is not None:
        current_lines.append(content)

if current_heading is not None and current_lines:
    blocks.append((current_heading, ''.join(current_lines)))

print(f'Extracted {len(blocks)} blocks', file=sys.stderr)

# ── generate C++ ──────────────────────────────────────────────────────────────

CPP_HEADER = '''\
// THIS FILE IS AUTO-GENERATED — do not edit by hand.
// Run modules/ai_assistant/gen_gotchas.py to regenerate.
//
// Contains all Godot gotcha entries, each indexed by keyword list.
// At runtime, only entries matching the user message are injected.

#include "ai_gotchas_index.h"

#include "core/string/ustring.h"

'''

def make_entry(heading, content, keywords):
    kw_strs = ', '.join(f'"{k}"' for k in keywords)
    # Escape content for C++
    lines_esc = []
    for part in content.split('\n'):
        lines_esc.append(f'        "{escape_cpp(part)}\\n"')
    content_cpp = '\n'.join(lines_esc)
    return f'    {{{{{kw_strs}}},\n{content_cpp}}}'


out_lines = [CPP_HEADER]
out_lines.append('const Vector<AIGotchasIndex::GotchaEntry> &AIGotchasIndex::_get_entries() {')
out_lines.append('    static const Vector<GotchaEntry> entries = {')

for i, (heading, content) in enumerate(blocks):
    kws = keywords_for_heading(heading)
    entry = make_entry(heading, content, kws)
    comma = ',' if i < len(blocks) - 1 else ''
    out_lines.append(entry + comma)

out_lines.append('    };')
out_lines.append('    return entries;')
out_lines.append('}')

cpp_output = '\n'.join(out_lines) + '\n'

with open('ai_gotchas_index.cpp', 'w', encoding='utf-8') as f:
    f.write(cpp_output)

print(f'Written ai_gotchas_index.cpp ({len(cpp_output)} chars, {len(out_lines)} lines)', file=sys.stderr)
print('Done.', file=sys.stderr)
