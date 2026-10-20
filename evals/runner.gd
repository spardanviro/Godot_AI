extends SceneTree
## Runs one eval task inside a headless editor and writes a JSON result.
##
## Started by run_evals.py as
##   godot --headless --editor --path <scratch project> --script runner.gd
## with the task and the provider passed through GODOT_AI_EVAL_* variables.
## The task is given to a real AIAgentController, so what is measured is the
## product's own loop: prompt assembly, the model's reply, the safety review,
## execution in the editor, and the automatic retries.

const OUTCOME_COMPLETED := 0
const OUTCOME_STOPPED := 1
const SETTLE_FRAMES := 5
const SCENE_OPEN_MAX_FRAMES := 600
const DEFAULT_TIMEOUT_SEC := 300.0

var _editor: Object
var _controller: Object
var _task: Dictionary
var _runs: Array[Dictionary] = []
var _notices: Array[String] = []
var _responses: Array[String] = []
var _plan_steps: PackedStringArray = []
var _executions: Array[Dictionary] = []


func _initialize() -> void:
	_main.call_deferred()


func _main() -> void:
	var result := {"passed": false, "error": ""}
	var task_path := OS.get_environment("GODOT_AI_EVAL_TASK")
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(task_path))
	if typeof(parsed) != TYPE_DICTIONARY:
		result["error"] = "Cannot read task file: " + task_path
		_finish(result)
		return
	_task = parsed
	result["id"] = _task.get("id", "")

	_editor = Engine.get_singleton("EditorInterface")
	await _wait_for_editor()
	await _apply_setup(_task.get("setup", {}))

	_apply_permissions(_task.get("setup", {}).get("permissions", {}))
	_controller = ClassDB.instantiate("AIAgentController")
	root.add_child(_controller)
	_controller.reload_settings()
	var provider := OS.get_environment("GODOT_AI_EVAL_PROVIDER")
	if not provider.is_empty():
		_controller.configure_provider(provider, OS.get_environment("GODOT_AI_EVAL_MODEL"),
				OS.get_environment("GODOT_AI_EVAL_ENDPOINT"), OS.get_environment("GODOT_AI_EVAL_API_KEY"))
	# Nobody is there to click "allow"; denied categories and blocked code are still refused.
	_controller.set_auto_approve(true)
	# The baseline a run with the look-up tools is compared against.
	if OS.get_environment("GODOT_AI_EVAL_TOOLS") == "0":
		_controller.set_tools_enabled(false)
	if _task.get("setup", {}).get("screenshot", false):
		_controller.set_pending_screenshot(_make_screenshot())
	_controller.run_finished.connect(func(report: Dictionary) -> void: _runs.append(report))
	_controller.notice.connect(func(_level: int, text: String) -> void: _notices.append(text))
	_controller.plan_ready.connect(func(plan: Dictionary) -> void: _plan_steps = plan.get("steps", PackedStringArray()))
	_controller.execution_finished.connect(func(report: Dictionary) -> void: _executions.append(report))
	_controller.response_received.connect(_on_response_received)
	# "stop_on": press Stop from inside one of the controller's signals, the
	# way a user can at the worst possible moment.
	var stop_on: String = _task.get("stop_on", "")
	if stop_on == "stream_delta":
		_controller.stream_delta.connect(func(_text: String, _thinking: bool) -> void: _controller.stop())
	elif stop_on == "response_received":
		_controller.response_received.connect(func(_response: Dictionary) -> void: _controller.stop())
	elif stop_on == "execution_finished":
		_controller.execution_finished.connect(func(_report: Dictionary) -> void: _controller.stop())

	var started_ms := Time.get_ticks_msec()
	var prompt: String = _task.get("prompt", "")
	if not _controller.submit_user_message(prompt, prompt):
		result["error"] = "The controller refused the message."
		_finish(result)
		return
	var timed_out := not await _wait_until_idle(started_ms)
	if not timed_out and _task.get("execute_plan", false) and _controller.has_pending_plan():
		_controller.execute_plan()
		timed_out = not await _wait_until_idle(started_ms)
	if timed_out:
		_controller.stop()
		result["error"] = "Timed out."
	await _wait_frames(SETTLE_FRAMES)

	result["duration_ms"] = Time.get_ticks_msec() - started_ms
	result["runs"] = _runs
	result["requests"] = _sum("requests")
	result["retries"] = _sum("retries")
	result["tool_calls"] = _sum("tool_calls")
	result["executions"] = _sum("executions")
	result["notices"] = _notices
	result["response"] = "\n\n".join(_responses)
	result["checks"] = _evaluate_checks(_task.get("checks", []))
	result["passed"] = not timed_out and result["checks"].all(func(check: Dictionary) -> bool: return check["passed"])
	_finish(result)


func _finish(result: Dictionary) -> void:
	var file := FileAccess.open(OS.get_environment("GODOT_AI_EVAL_RESULT"), FileAccess.WRITE)
	if file:
		file.store_string(JSON.stringify(result, "\t"))
		file.close()
	else:
		push_error("Cannot write the eval result file.")
	quit(0)


func _on_response_received(response: Dictionary) -> void:
	var parts := PackedStringArray(response.get("text_segments", PackedStringArray()))
	for block: String in response.get("code_blocks", PackedStringArray()):
		parts.append("```gdscript\n" + block + "\n```")
	_responses.append("\n".join(parts))


func _sum(key: String) -> int:
	var total := 0
	for run: Dictionary in _runs:
		total += int(run.get(key, 0))
	return total


# --- Waiting ---------------------------------------------------------------

func _wait_frames(count: int) -> void:
	for i in count:
		await process_frame


func _wait_for_editor() -> void:
	await _wait_frames(SETTLE_FRAMES)
	var filesystem: Object = _editor.get_resource_filesystem()
	while filesystem.is_scanning():
		await process_frame
	await _wait_frames(SETTLE_FRAMES)


## Returns false if the task's time budget ran out first.
func _wait_until_idle(started_ms: int) -> bool:
	var timeout_sec := float(OS.get_environment("GODOT_AI_EVAL_TIMEOUT")) if OS.has_environment("GODOT_AI_EVAL_TIMEOUT") else DEFAULT_TIMEOUT_SEC
	var deadline_ms := started_ms + int(timeout_sec * 1000.0)
	while _controller.is_busy():
		if Time.get_ticks_msec() > deadline_ms:
			return false
		await process_frame
	return true


# --- Setup -----------------------------------------------------------------

## setup = { "scene_root": "Node2D", "nodes": [{ "path": "Player", "class": "CharacterBody2D",
##           "properties": {...} }], "files": { "res://x.gd": "..." } }
## A stand-in for a viewport capture, queued to go out with the prompt.
func _make_screenshot() -> String:
	var image := Image.create(16, 16, false, Image.FORMAT_RGB8)
	image.fill(Color(0.2, 0.4, 0.8))
	var path := "user://eval_screenshot.png"
	image.save_png(path)
	return path


## setup.permissions = { "delete_nodes": 2 }: levels (0 allow, 1 ask, 2 deny) by
## category. They are editor settings, so they are only written when the run
## has an editor configuration of its own.
func _apply_permissions(permissions: Dictionary) -> void:
	if permissions.is_empty():
		return
	if OS.get_environment("GODOT_AI_EVAL_ISOLATED") != "1":
		push_warning("This task sets permissions and is skipped with --use-editor-config: it would change your settings.")
		return
	var settings: Object = _editor.get_editor_settings()
	for category: String in permissions:
		settings.set_setting("ai_assistant/permissions/" + category, int(permissions[category]))


func _apply_setup(setup: Dictionary) -> void:
	var files: Dictionary = setup.get("files", {})
	for path: String in files:
		DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(path.get_base_dir()))
		var file := FileAccess.open(path, FileAccess.WRITE)
		file.store_string(files[path])
		file.close()

	var root_class: String = setup.get("scene_root", "")
	if not root_class.is_empty():
		var scene_root: Node = ClassDB.instantiate(root_class)
		scene_root.name = "Main"
		for spec: Dictionary in setup.get("nodes", []):
			var node_path: String = spec["path"]
			var node: Node = ClassDB.instantiate(spec["class"])
			node.name = node_path.get_file()
			var parent_path := node_path.get_base_dir()
			var parent: Node = scene_root if parent_path.is_empty() else scene_root.get_node(parent_path)
			parent.add_child(node)
			node.owner = scene_root
			var properties: Dictionary = spec.get("properties", {})
			for property: String in properties:
				node.set(property, _to_variant(properties[property]))
		var packed := PackedScene.new()
		packed.pack(scene_root)
		ResourceSaver.save(packed, "res://main.tscn")
		scene_root.free()

	if not files.is_empty() or not root_class.is_empty():
		_editor.get_resource_filesystem().scan()
		await _wait_for_editor()
	if not root_class.is_empty():
		_editor.open_scene_from_path("res://main.tscn")
		var waited_frames := 0
		while _editor.get_edited_scene_root() == null and waited_frames < SCENE_OPEN_MAX_FRAMES:
			waited_frames += 1
			await process_frame
		await _wait_frames(SETTLE_FRAMES)


## JSON has no vectors or colours: strings such as "Vector2(1, 2)" are parsed.
func _to_variant(value: Variant) -> Variant:
	if typeof(value) == TYPE_STRING:
		var parsed: Variant = str_to_var(value)
		if parsed != null:
			return parsed
	return value


# --- Checks ----------------------------------------------------------------

## A Label's effective font colour and size, whichever way they were set: a
## theme override or a LabelSettings resource. (Expressions do not
## short-circuit, so "x != null and x.y" cannot be written in a check.)
func label_font_color(path: String) -> Color:
	var label := node(path) as Label
	if label == null:
		return Color.BLACK
	if label.label_settings != null:
		return label.label_settings.font_color
	return label.get_theme_color("font_color")


func label_font_size(path: String) -> int:
	var label := node(path) as Label
	if label == null:
		return 0
	if label.label_settings != null:
		return label.label_settings.font_size
	return label.get_theme_font_size("font_size")


## Number of nodes the SAVED scene would have in a group. A group added
## without persistent=true exists in memory only and does not count.
func saved_group_members(group: String) -> int:
	var scene := _scene()
	if scene == null:
		return 0
	var packed := PackedScene.new()
	if packed.pack(scene) != OK:
		return 0
	var state := packed.get_state()
	var count := 0
	for index in state.get_node_count():
		if state.get_node_groups(index).has(group):
			count += 1
	return count


func _evaluate_checks(checks: Array) -> Array:
	var results: Array = []
	for check: Dictionary in checks:
		var detail := _run_check(check)
		results.append({"type": check.get("type", ""), "check": check, "passed": detail.is_empty(), "detail": detail})
	return results


func _scene() -> Node:
	return _editor.get_edited_scene_root()


func _node(path: String) -> Node:
	var scene := _scene()
	if scene == null:
		return null
	return scene if path.is_empty() or path == "." else scene.get_node_or_null(path)


## Every node of class `type` in the edited scene (helper for expression checks).
func find(type: String) -> Array[Node]:
	var found: Array[Node] = []
	var pending: Array[Node] = []
	if _scene() != null:
		pending.append(_scene())
	while not pending.is_empty():
		var node: Node = pending.pop_back()
		if node.is_class(type):
			found.append(node)
		pending.append_array(node.get_children())
	return found


## The node at `path` below the scene root, or null (helper for expression checks).
func node(path: String) -> Node:
	return _node(path)


## The `text` of every node of class `type`.
func texts(type: String) -> Array[String]:
	var result: Array[String] = []
	for found_node: Node in find(type):
		result.append(str(found_node.get("text")))
	return result


## How many nodes of the edited scene are in `group`.
func group_members(group: String) -> int:
	var count := 0
	for found_node: Node in find("Node"):
		if found_node.is_in_group(group):
			count += 1
	return count


## [root name, root class] of a saved scene, or [] if it cannot be loaded.
func scene_root_info(path: String) -> Array:
	var packed: Variant = ResourceLoader.load(path, "", ResourceLoader.CACHE_MODE_IGNORE) if FileAccess.file_exists(path) else null
	if not packed is PackedScene:
		return []
	var instance: Node = packed.instantiate()
	var info := [String(instance.name), instance.get_class()]
	instance.free()
	return info


## Lines starting with `prefix` across the saved plan checklists ("- [x] " / "- [ ] ").
func plan_lines(prefix: String) -> int:
	var count := 0
	for file_name: String in DirAccess.get_files_at("res://ai_plans"):
		for line: String in _read("res://ai_plans".path_join(file_name)).split("\n"):
			if line.begins_with(prefix):
				count += 1
	return count


## For "expression" checks: takes back the last action of the scene's undo
## history, as Ctrl+Z would. True if there was one.
func undo_last() -> bool:
	var manager: Object = _editor.get_editor_undo_redo()
	var history: UndoRedo = manager.get_history_undo_redo(manager.get_object_history_id(_scene()))
	return history.undo()


## For "expression" checks: whether a connection saved with the scene exists.
func connected(source: String, signal_name: String, target: String, method: String) -> bool:
	var from := _node(source)
	var to := _node(target)
	if from == null or to == null:
		return false
	for connection: Dictionary in from.get_signal_connection_list(signal_name):
		var callable: Callable = connection["callable"]
		if callable.get_object() == to and callable.get_method() == method and (int(connection["flags"]) & CONNECT_PERSIST) != 0:
			return true
	return false


func _last_run() -> Dictionary:
	return _runs.back() if not _runs.is_empty() else {}


func _read(path: String) -> String:
	return FileAccess.get_file_as_string(path) if FileAccess.file_exists(path) else ""


## Returns "" when the check passes, otherwise why it failed.
func _run_check(check: Dictionary) -> String:
	var type: String = check.get("type", "")
	match type:
		"run_completed":
			return "" if int(_last_run().get("outcome", -1)) == OUTCOME_COMPLETED else "run did not complete: " + str(_last_run().get("error", ""))
		"run_stopped":
			return "" if int(_last_run().get("outcome", -1)) == OUTCOME_STOPPED else "run was not stopped (outcome %s)" % _last_run().get("outcome", -1)
		"executed_ok":
			return "" if not _executions.is_empty() and _executions.back()["success"] else "no successful execution"
		"no_execution":
			return "" if _executions.is_empty() else "code was executed"
		"retries_max":
			return "" if _sum("retries") <= int(check["value"]) else "retries: %d" % _sum("retries")
		"retries_equal":
			return "" if _sum("retries") == int(check["value"]) else "retries: %d" % _sum("retries")
		"tool_calls_equal":
			return "" if _sum("tool_calls") == int(check["value"]) else "tool calls: %d" % _sum("tool_calls")
		"tool_calls_min":
			return "" if _sum("tool_calls") >= int(check["value"]) else "tool calls: %d" % _sum("tool_calls")
		"requests_max":
			return "" if _sum("requests") <= int(check["value"]) else "requests: %d" % _sum("requests")
		"plan_steps_min":
			return "" if _plan_steps.size() >= int(check["value"]) else "plan steps: %d" % _plan_steps.size()
		"node_exists":
			var node := _node(check["path"])
			if node == null:
				return "node not found: " + check["path"]
			if check.has("class") and not node.is_class(check["class"]):
				return "%s is a %s, expected %s" % [check["path"], node.get_class(), check["class"]]
			return ""
		"node_missing":
			return "" if _node(check["path"]) == null else "node still exists: " + check["path"]
		"node_count":
			var count := find(check["class"]).size()
			if check.has("min") and count < int(check["min"]):
				return "%d %s nodes, expected at least %d" % [count, check["class"], int(check["min"])]
			if check.has("max") and count > int(check["max"]):
				return "%d %s nodes, expected at most %d" % [count, check["class"], int(check["max"])]
			return ""
		"all_nodes_owned":
			# A node without an owner is not saved with the scene.
			for node: Node in find("Node"):
				if node != _scene() and node.owner != _scene():
					return "node has no owner: " + str(_scene().get_path_to(node))
			return "" if _scene() != null else "no scene is open"
		"property":
			var node := _node(check["path"])
			if node == null:
				return "node not found: " + check["path"]
			var actual: Variant = node.get(check["property"])
			var expected: Variant = _to_variant(check["equals"])
			if typeof(actual) in [TYPE_FLOAT, TYPE_INT] and typeof(expected) in [TYPE_FLOAT, TYPE_INT]:
				return "" if is_equal_approx(float(actual), float(expected)) else "%s = %s" % [check["property"], actual]
			return "" if actual == expected else "%s = %s" % [check["property"], var_to_str(actual)]
		"has_script":
			var node := _node(check["path"])
			return "" if node != null and node.get_script() != null else "no script on " + check["path"]
		"file_exists":
			return "" if FileAccess.file_exists(check["file"]) else "missing file: " + check["file"]
		"file_missing":
			return "" if not FileAccess.file_exists(check["file"]) else "file exists: " + check["file"]
		"file_contains", "file_contains_any":
			var content := _read(check["file"])
			var needles: Array = check["any"] if check.has("any") else [check["text"]]
			for needle: String in needles:
				if content.contains(needle):
					return ""
			return "%s does not contain %s" % [check["file"], needles]
		"file_not_contains":
			return "" if not _read(check["file"]).contains(check["text"]) else "%s contains %s" % [check["file"], check["text"]]
		"script_compiles":
			if not FileAccess.file_exists(check["file"]):
				return "missing file: " + check["file"]
			var script: Variant = ResourceLoader.load(check["file"], "", ResourceLoader.CACHE_MODE_IGNORE)
			if not script is GDScript:
				return "not a GDScript: " + check["file"]
			return "" if script.reload(true) == OK else "script does not compile: " + check["file"]
		"response_contains_any":
			var response := "\n".join(_responses).to_lower()
			for needle: String in check["any"]:
				if response.contains(needle.to_lower()):
					return ""
			return "response mentions none of %s" % [check["any"]]
		"expression":
			var expression := Expression.new()
			if expression.parse(check["code"], ["root"]) != OK:
				return "bad expression: " + expression.get_error_text()
			var value: Variant = expression.execute([_scene()], self, false)
			if expression.has_execute_failed():
				return "expression failed: " + expression.get_error_text()
			return "" if value else "false: " + check["code"]
	return "unknown check type: " + type
