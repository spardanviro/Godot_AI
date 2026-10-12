---
id: audio
order: 80
match: stem
keywords:
  - audio
  - sound
  - music
  - audiobus
  - audiostreamplayer
  - 音频
  - 音乐
  - 声音
  - 音效
---

## Audio System Specialization

### AudioStreamPlayer Setup
```gdscript
# 2D positional audio
var player = AudioStreamPlayer2D.new()
player.name = "SFXPlayer"
player.stream = load("res://audio/sfx/explosion.wav")
player.volume_db = -6.0
player.max_distance = 500.0
parent.add_child(player)
player.owner = scene_root

# Non-positional (BGM)
var bgm = AudioStreamPlayer.new()
bgm.name = "BGMPlayer"
bgm.stream = load("res://audio/music/theme.ogg")
bgm.bus = "Music"  # Route to Music bus
bgm.autoplay = true
```

### Audio Bus Management (in EditorScript)
```gdscript
# Godot manages buses via AudioServer at runtime.
# In EditorScript, create default_bus_layout.tres:
var layout = AudioBusLayout.new()
# Bus 0 is always Master. Add new buses:
AudioServer.add_bus()        # Bus 1
AudioServer.set_bus_name(1, "Music")
AudioServer.set_bus_volume_db(1, -3.0)
AudioServer.add_bus()        # Bus 2
AudioServer.set_bus_name(2, "SFX")
# Add effects:
var reverb = AudioEffectReverb.new()
AudioServer.add_bus_effect(1, reverb)
```
