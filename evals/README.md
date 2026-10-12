# Evals

A fixed set of tasks that says, with a number, how well the assistant does its job. Run it before and after changing a prompt, a gotcha, a domain file or the agent loop.

Each task is a prompt plus checks on the result: which nodes exist, what their properties are, whether a script compiles, what a file contains, whether code ran at all. A task is given to a real `AIAgentController` inside a headless editor, so the whole product loop is measured — prompt assembly, the model's reply, the safety review, execution in the editor, and the automatic retries.

## Running

```bash
# 1. Check the harness and the agent loop. No API key, about a minute.
python run_evals.py --godot ../../../bin/godot.windows.editor.x86_64.console.exe --mock

# 2. Score a real model.
python run_evals.py --godot <editor> --provider deepseek --out results/baseline.json

# 3. Try a change to the knowledge files (no rebuild needed) and compare.
python run_evals.py --godot <editor> --provider deepseek --knowledge-dir ../knowledge \
    --baseline results/baseline.json
```

`--godot` must be an editor build that contains this module (on Windows, the `.console.exe`).

Every task runs in its own editor process, in a fresh copy of `project_template/`, with a private editor configuration — so tasks cannot affect each other, a crash only loses one task, and your own editor settings and chat history are not touched.

### Mock mode

`--mock` starts a local OpenAI-compatible server that replays the `mock_replies` stored with each task. It is deterministic and free, and it proves two things: every task's checks can be satisfied (the stored reply is a reference answer), and the agent loop behaves — including the `selftest.*` tasks, which script situations a real model only produces by accident: a syntax error followed by a fix, a runtime error, a reply without code, an error raised after the code returned, a plan carried out step by step, reasoning in `<think>` tags, a memory note, non-ASCII output split across network chunks.

With `--mock` the run fails (exit code 1) unless every task passes.

### Live mode

Without `--mock` the tasks go to a real model. The API key is read from the provider's environment variable (`GODOT_AI_API_KEY_<PROVIDER>` or the vendor's own, e.g. `DEEPSEEK_API_KEY`). `--provider`, `--model` and `--endpoint` choose what to talk to. Pass `--use-editor-config` to use the provider and key configured in your editor instead.

Model output varies between runs: with one run per task, a single task is worth about three points of pass rate. Use `--repeat 3` (or more) for a number worth comparing; `--filter` reruns a subset cheaply.

Runs that end without a verdict — the editor crashed or timed out — are reported on their own line and left out of the rates: they say nothing about the model. The results file records a fingerprint of the knowledge files, so two files can be told apart later.

### Options

| Option | Meaning |
|--------|---------|
| `--filter TEXT` | Only tasks whose id contains TEXT |
| `--jobs N` | Tasks in parallel (default 2) |
| `--repeat N` | Run every task N times (default 1); rates are then over all runs |
| `--timeout SEC` | Budget per task once the editor is up (default 300) |
| `--knowledge-dir DIR` | Knowledge files to use instead of the built-in ones |
| `--out FILE` | Write results as JSON |
| `--baseline FILE` | Compare with an earlier `--out` file: pass rate, tasks fixed, tasks broken. A task that passed there and fails now makes the exit code 1 |
| `--allow-regressions` | With `--baseline`: report regressions without failing |
| `--min-pass-rate R` | Exit with an error below this rate (default 1.0 with `--mock`) |
| `--keep-projects` | Keep the scratch projects of passed tasks (failed ones are always kept) |
| `--verbose` | Show the tail of the editor log for failed tasks |

## What is reported

```text
task                               result   req retry    sec
scene.add_sprite                   passed     1     0    4.6
script.godot3_to_4                 failed     4     3   41.0
    - script_compiles: script does not compile: res://legacy.gd
...
Passed     27/30 (90%)
First try  22/30 (73%)
Requests   41 (11 automatic retries)
```

- **Passed** — all checks of the task hold.
- **First try** — passed without the loop having to ask the model again. This is the number that moves first when prompt quality changes: retries hide a lot.
- **Requests / retries** — what the result cost.

## Tasks

`tasks/*.json` each hold a list of tasks:

```json
{
  "id": "scene.add_sprite",
  "mode": "agent",
  "setup": { "scene_root": "Node2D" },
  "prompt": "Add a Sprite2D named \"Logo\" to the scene at position (100, 50).",
  "checks": [
    { "type": "run_completed" },
    { "type": "node_exists", "path": "Logo", "class": "Sprite2D" },
    { "type": "property", "path": "Logo", "property": "position", "equals": "Vector2(100, 50)" },
    { "type": "all_nodes_owned" }
  ],
  "mock_replies": [["```gdscript", "var root = get_scene()", "...", "```"]]
}
```

- `id` — `<category>.<name>`; the category is used for the per-category summary.
- `mode` — `ask`, `agent` or `plan`. `"execute_plan": true` also runs the plan that PLAN mode produced.
- `setup` — what exists before the prompt is sent:
  - `scene_root`: class of the root of an open scene (`res://main.tscn`); omit for "no scene open".
  - `nodes`: nodes to create under it, `{ "path", "class", "properties" }`.
  - `files`: `{ "res://path": content }`.
  - `screenshot`: `true` attaches a (synthetic) viewport screenshot to the prompt.
- `prompt`, `mock_replies` entries and file contents may be a string or a list of lines.
- `mock_replies` — one reply per request, in order; the last one is repeated if the loop asks again. Besides text, a reply can be `{ "status": 400, "body": "..." }` (the API answers with an error) or `{ "requires_image": true, "text": ... }` (the text is only sent if the request carries an image), or `{ "finish": "length", "text": ... }` (the stream ends like a reply cut off at the output limit).
- `"mock_only": true` — only meaningful with canned replies; skipped in live runs.
- `"stop_on"` — `stream_delta`, `response_received` or `execution_finished`: press Stop from inside that signal (self-tests for "Stop is final").

### Checks

| Type | Passes when |
|------|-------------|
| `run_completed` | The (last) run ended normally, not stopped or failed |
| `run_stopped` | The (last) run was stopped |
| `executed_ok` / `no_execution` | The last execution succeeded / no code was executed at all |
| `retries_max`, `retries_equal`, `requests_max` (`value`) | Bounds on what the loop needed |
| `plan_steps_min` (`value`) | PLAN mode produced at least this many numbered steps |
| `node_exists` (`path`, optional `class`) | The node exists below the scene root and is of that class |
| `node_missing` (`path`) | It does not |
| `node_count` (`class`, `min` / `max`) | Number of nodes of a class in the scene |
| `all_nodes_owned` | Every node has the scene root as owner (unowned nodes are not saved) |
| `property` (`path`, `property`, `equals`) | Property value; `"Vector2(1, 2)"`-style strings are parsed |
| `has_script` (`path`) | A script is attached |
| `script_compiles` (`file`) | The script loads and compiles |
| `file_exists`, `file_missing` (`file`) | |
| `file_contains` (`file`, `text`), `file_contains_any` (`any`), `file_not_contains` | Text search in a file |
| `response_contains_any` (`any`) | The reply mentions one of these (case-insensitive) |
| `expression` (`code`) | A GDScript expression is true. `root` is the scene root; helpers: `node(path)`, `find(class)`, `texts(class)`, `group_members(group)`, `saved_group_members(group)` (only groups that are saved with the scene), `label_font_color(path)`, `label_font_size(path)`, `scene_root_info(path)`, `plan_lines(prefix)` |

### Writing a good task

- Check the outcome, not the wording: "a `CollisionShape2D` with a `RectangleShape2D` exists under `Player`", not "the reply contains `RectangleShape2D.new()`".
- Say in the prompt everything the checks rely on (names, values). A task that fails because the model chose a different but reasonable name measures nothing.
- Add the checks that catch the classic mistakes for the area — `all_nodes_owned` for anything that adds nodes, `script_compiles` for anything that writes a script.
- Always store a reference answer in `mock_replies` and run `--mock --filter <id>`: if the reference answer does not pass, the checks are wrong.

## Checking the knowledge files against the engine

### Tool calls

`tasks/tools.json` holds the tasks for tool calling (including the web, asset and memory tools, which are exercised on the paths that need no network: refused addresses, the request limit, a missing key); in a mock run each one scripts the model's side of the exchange. A canned reply can carry tool calls and state what the request it answers must look like:

```json
{ "requires_tool_result": "position = Vector2(120, 80)",
  "tool_calls": [ { "name": "set_node_properties", "arguments": { "path": "Player", "properties": { "visible": false } } } ] }
```

`requires_tools` (true / false), `requires_tool_result` and `forbids` are checked against the request; if one does not hold, a sentence without code is sent instead, so the task fails. Checks `tool_calls_equal` / `tool_calls_min` count the calls; in an `expression` check, `undo_last()` steps the scene's undo history back and `connected(source, signal, target, method)` tests a saved connection.

To measure what the tools are worth with a real model, run the same tasks with and without them:

```bash
python run_evals.py --godot <editor> --provider deepseek --no-tools --out without.json
python run_evals.py --godot <editor> --provider deepseek --baseline without.json
```

`check_layers.py` checks that the module's folders only include downwards and that the editor's settings are read in one place (see "Layers" in the main README). It needs nothing but the sources:

```bash
python check_layers.py
```

`check_knowledge_refs.py` reads every `Class.member` the knowledge files mention and looks it up in the engine's class documentation (inheritance included). It catches renamed and misremembered API names; it cannot judge prose.

```bash
python check_knowledge_refs.py ../../.. ../knowledge
```

Lines that show what not to write (WRONG, removed, renamed, ...) are skipped. Run it after editing `gotchas.md` or `api_docs.md`.
