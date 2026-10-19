# Godot AI Assistant

**English** | [简体中文](README.zh-CN.md)

AI assistant support inside the Godot editor as a native `ai_assistant` module, compatible with **Godot 4.7+** (godot-master).

This project adds a docked AI panel to the editor, supports multiple LLM providers, and can execute AI-generated editor actions through `EditorScript` workflows. It is built for scene editing, script generation, project setup, file-aware prompting, asset generation, and debugging assistance directly inside Godot.

## Overview

`ai_assistant` is a native C++ Godot module. It is not a GDExtension and not a regular addon distributed as project files. It is intended to live under the Godot engine source tree and be compiled into the editor build.

Once enabled, the module registers:

- `AIAssistantPlugin` as an editor plugin
- `AIAssistantPanel` as the main dock UI
- Provider integrations for Anthropic, OpenAI-compatible APIs, Gemini, DeepSeek, and GLM
- Helper systems for response parsing, script execution, context collection, permissions, checkpoints, error monitoring, web search, asset generation, and mention-aware input
- On-demand API doc and gotchas injection (zero static overhead for unrelated topics)

The panel is mounted in the right dock area of the editor.

## Implemented Features

### Editor assistant panel

- Docked AI panel inside the Godot editor
- Chat-style interaction UI with persistent conversation history
- `New`, `History`, `Settings`, file attachment, mention, send, and stop controls
- Streamed responses with a `Stop` button for partial cancellation
- Replies rendered as Markdown, also while they stream in: headings, bold / italic / strikethrough, inline code, fenced code blocks, nested and numbered lists, task lists, quotes, rules, tables, and web links that open in the browser
- Auto-resizing multiline input field
- High-frequency preset actions:
  - `Fix Errors`
  - `Performance`

### Assistant modes

- `ASK`: conversational mode, no code execution
- `AGENT`: action-oriented mode with execution flow
- `PLAN`: planning-first mode before execution

### Node and file mentions and editor-aware input

- Type `@` in the input box to open autocomplete for scene nodes; once at least one character follows the `@`, matching project files are listed too
- Drag scene nodes from the Scene dock, or files and folders from the FileSystem dock, into the input field to insert mention chips
- Use the `@` toolbar button to mention the current scene selection
- Right-click nodes in the Scene dock or entries in the FileSystem dock to mention them from the context menu
- Mention chips are rendered inline (files in green, nodes in blue) and expanded before the request is sent: nodes into structured node context, files into their contents (capped at 50,000 characters), folders into a listing of their entries
- Mention placeholders are atomic single-character tokens, so backspace removes them cleanly

### File attachments

- Attach project files for the next request through an editor file picker
- Attached file contents are appended to the outgoing prompt as explicit context
- Supported attachment filters:
  - Markdown / diagram: `*.md`, `*.mmd`, `*.mermaid`
  - Text / data: `*.txt`, `*.json`, `*.yaml`, `*.yml`, `*.toml`, `*.csv`
  - GDScript: `*.gd`, `*.gdscript`

### Provider support

- Anthropic (Claude)
- OpenAI
- Gemini
- DeepSeek
- GLM (Zhipu AI)

The OpenAI provider path can also be pointed at compatible custom endpoints through the endpoint setting.

The model is a text field: any model name can be typed, which is what an OpenAI-compatible relay or aggregator (SiliconFlow, OpenRouter, a local server) needs; set its address as the API endpoint. The button beside the field lists the known models, and `Test / Refresh` replaces that list with the one the endpoint reports — cut down to current chat models for the provider's own API, whole for any other service. It keeps a separate API key per provider and swaps them automatically when you change providers.

OpenAI, DeepSeek and GLM speak the same chat-completions wire format and share one implementation (`OpenAICompatibleProvider`); each of them only declares what differs (endpoint, model tables, the name of the output-limit field).

### Model facts and thinking

What a model allows comes from [models.dev](https://models.dev), which tracks it per provider and model: context window, output limit, whether it takes a temperature, and its thinking settings. A copy is built in (`data/model_specs.json`, refreshed with `python update_model_specs.py`); the editor also downloads newer data on its own when its copy is more than a week old, and keeps it in `<editor config>/ai_assistant/model_specs.json`. A model the data does not list falls back to the provider's own table.

- **Max tokens** follows the selected model by default: its output limit, unless that would leave less than three quarters of the context window for the prompt and history. Tick *Custom* to fix a value.
- **Temperature** is the model's own default unless *Custom* is ticked; for a model that takes none the option is disabled.
- **Thinking** lists what the selected model offers, in its maker's words: `low` / `high` / `max` for DeepSeek V4, `low` to `max` (five levels) for current Claude models, `none` to `xhigh` for GPT, `minimal` to `high` for some Gemini models, *Off* where thinking can be switched off. *Model default* sends nothing. Each provider gets it in its own form (`reasoning_effort`, `thinking.type`, `output_config.effort`, `thinkingConfig.thinkingLevel`).

Older models that take a thinking budget instead (Claude Haiku 4.5, Gemini 2.5) are offered budgets of 4096 / 16384 / 32768 tokens, within what the model allows.

The default model comes from the same data: the newest model of each provider's flagship series (`claude-opus`, `gpt-sol`, `gemini` / `gemini-pro`, `glm`, `deepseek-thinking`), so it moves on when the data does. From a model list fetched from the provider, the default is picked when listed; otherwise the version in the names decides (a generation newer than the data shows up there first), then the release dates in the data.

Thinking models need their earlier reasoning back in a tool loop:

- DeepSeek and GLM require the reasoning of earlier replies (`reasoning_content`) to be sent back; the assistant does so for them.
- Claude's thinking blocks are signed. The blocks of the current tool loop go back unchanged, and the API is asked to drop a block it no longer accepts instead of refusing the request (`block_binding.prefix_mismatch_behavior: "drop_block"`). The system prompt carries the editor's state and changes between requests, so some blocks are dropped; that is never worse than not sending them. A relay that rejects the parameter makes the assistant retry once without it.

A thinking model that calls tools often writes no text, only reasoning and calls; such a reply is acted on as calls, not shown as "reasoning without an answer".

### On-demand API reference injection (`AIAPIDocLoader`)

The system prompt stays lean. Instead of statically including all Godot API documentation, `AIAPIDocLoader` detects Godot class names mentioned in the user's message (word-boundary matched) and injects compact API references for only those classes. Covers 50+ commonly-used classes including `CharacterBody3D`, `NavigationAgent3D`, `AnimationPlayer`, `RigidBody3D`, and more.

### On-demand gotchas injection (`AIGotchasIndex`)

395 Godot-specific gotchas and pitfalls are stored in `knowledge/gotchas.md`, each with its keywords. At request time, only entries whose keywords match the user's message are injected, most relevant first, within a hard 3000-character budget. This replaces static sections that were injected on every request regardless of relevance.

Matching ignores generic keywords (ordinary English words, `gdscript`, `node`, `int`, ...) and weights the rest by rarity, so an entry needs at least one reasonably specific hit — typically a class or method name — to be injected. A message that names no specific API gets no gotchas. Class names are matched even when written directly against CJK text.

The always-on base prompt is about 11K characters (~3K tokens) for a model that works with tools, and about 33K characters for one that writes code blocks, which also has to be taught how an `EditorScript` builds a scene. `system_prompt.md` holds both: what is not for every model sits between `<!-- SCRIPT-ONLY -->`, `<!-- TOOLS-ONLY -->` and `<!-- MARKERS-ONLY -->` lines.

### Domain-specific prompting (`AIDomainPrompts`)

Specialized system prompt fragments for domain-focused requests (UI/Control layout, physics, navigation, shaders, etc.) are injected on demand alongside the base system prompt.

### Code generation and execution

- Parses AI replies and extracts executable GDScript blocks
- Wraps generated code as `EditorScript`
- Executes code in the editor context
- Supports compile-only validation before execution
- Checks generated code against a safety policy before execution
- Integrates permission checks for sensitive operations
- Skips code execution in `ASK` mode
- If a streamed response is manually stopped, partial code is preserved in history but not executed

Safety policy for editor-executed code (checked on the token stream, so comments and string contents are not mistaken for code):

- `OS`, `Engine` and `ClassDB` may only be used as `Name.method(...)` with a harmless method (`OS.get_name()`, `Engine.is_editor_hint()`, `ClassDB.class_exists()`, ...). They cannot be assigned, passed or indexed, so process-spawning methods cannot be reached through an alias, `Engine.get_singleton()` or `ClassDB.instantiate()`
- `Expression`, `JavaClassWrapper`, `JavaScriptBridge`, `GDExtensionManager` and `EditorSettings` are rejected, as is any access to the assistant's own settings (permissions) and to the editor's data directories (where the credential store lives). `OS.get_environment()` is not on the allowed list either, so keys supplied through environment variables are out of reach
- Network calls (`request`, `connect_to_host`, `listen`, ...) are rejected. Creating networking nodes for the game is fine; using the network from the editor is not
- `ProjectSettings.globalize_path()` is only accepted in the form `DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path("res://folder"))`; the other ways of obtaining a real filesystem path (`OS.get_user_data_dir()`, ...) are rejected
- String literals may not name absolute paths, UNC or `file://` locations, or `res://`/`user://` paths that climb out with `..`
- When code is rejected, the reason is sent back to the model so it can rewrite within the rules

This is static analysis, not a sandbox. It cannot see values assembled at run time: a path or method name built from pieces passes the check. Review generated code before running it on a machine or project you care about, and keep auto-execute off if you want to approve each run.

### Tools

With a provider that speaks tool calls — all five do — the model works on the project by calling typed tools instead of writing code to be run. Turn it off with *Settings > General > Tools*; a model or endpoint that rejects tool calls is detected on its first request and used the old way from then on.

| Kind | Tools | |
|------|-------|-|
| Look | `get_scene_tree`, `get_node_properties`, `read_file`, `list_files`, `get_errors` | Change nothing and need no permission. With the web tools, the only ones offered in ASK and PLAN modes. |
| Web | `web_search`, `fetch_url` | Look something up outside the project: public http(s) addresses only, at most 3 requests per message. What comes back is handed to the model marked as untrusted content. |
| Edit | `create_node`, `instantiate_scene`, `set_node_properties`, `rename_node`, `move_node`, `delete_node`, `connect_signal`, `attach_script`, `write_file`, `save_scene`, `set_project_setting`, `add_input_action` | Each falls under one permission category (*Settings > Permissions*): allowed, asked about, or refused, per tool call. |
| Host | `run_scene`, `take_screenshot`, `run_editor_script`, `generate_image`, `generate_speech`, `remember` | Run the game for a few seconds and report its errors, what it printed and (on request) a frame of it; show the editor viewport to the model; run arbitrary editor code; have an asset generated; write one line into `godot_ai.md`. Generating an asset follows the *Write Files* permission; a note has a permission of its own. |

- **Undo.** Every scene change a tool makes is one action of the editor's undo history, named after what it did: Ctrl+Z takes it back like a change made by hand. Before the first change for a message a checkpoint is taken, and the transcript gets a link that reverts everything that follows, files included.
- **Values.** Arguments are JSON; engine values are written as GDScript writes them (`"Vector2(100, 50)"`), as a list of components (`[100, 50]`) or as a colour code. A resource is a `res://` path or is created in place: `{"$resource": "RectangleShape2D", "size": "Vector2(32, 32)"}`. A value cannot bring code with it: text that would build an object (`Object(GDScript, ...)`) is not parsed, a script or scene resource is not created in place, and `script` is not a property a tool sets. Scripts come from files, through `write_file` and `attach_script`, under the permission for scripts; so does a node created with a script on it.
- **Files.** `read_file` and `write_file` stay inside `res://`, skip hidden folders, and do not touch `export_presets.cfg` (signing keys) or files the editor owns (`.tscn`, `.import`, `project.godot`). A script that does not parse is not written.
- **What is asked about is shown.** A prompt for `write_file` carries the file: the whole of a new one, and for one that exists the lines that go and come. A prompt for `remember` carries the note.
- **The project.** `set_project_setting` changes a setting that exists, to a value of its type; autoloads and editor plugins — what decides which code runs — are left to the user. `add_input_action` registers an action with keys (by position on the keyboard, as the Input Map does) and mouse buttons. Both save `project.godot`, which the checkpoint covers.
- **The running game.** `run_scene` reports the errors the game raised and what it printed, and with `screenshot: true` a frame of it, asked of the game over the debugger connection just before it is stopped. That is how the model checks anything visual; `take_screenshot` shows the editor's viewport.
- **Notes ask.** A line written with `remember` is part of the instructions of every later conversation, and what prompts it may be text the model read in a file or on a web page. *Remember Notes* is therefore a permission of its own and asks by default.
- **`run_editor_script`** is the fallback for what no tool does (animations, tile maps, bulk changes). It goes through the same review as a code block — syntax, safety policy, permissions — but that review reads the code as text, and code can build at run time what the review looks for. So the user decides: the whole of the code is shown, in a scrolling view of the permission prompt, and runs only once they have allowed it, every time, whatever the auto-execute setting says. Its outcome is the tool's result: what the code `print()`ed, or the error it raised.
- **A code block is something to read.** A model that has the tools runs code by calling `run_editor_script`. A code block in its reply — a script it explains, a command for the user's terminal — is shown and never executed.
- **A text answer is an answer.** A model that has the tools and replies to the user in text (a refusal, a question back) is not pushed to "output the code block" the way a model without tools is. Only a step it was sent to carry out — a plan step, a runtime fix — is asked for once more, and then with the tools.
- **Errors are results.** A tool never fails the run: what went wrong is handed back as text for the model to act on. A message may take at most 8 rounds of tool calls.

#### Without tools (legacy)

All five providers speak tool calls, so this path exists for the endpoints that reject them. It is kept working and covered by the eval set, but it is not where new work goes: it needs a prompt three times the size, a parser for code fences, automatic repairs of the code models tend to write, and a safety check that reads code as text.

A model or endpoint without tool calls still works the old way: it writes a `gdscript` code block, which is reviewed and executed under the permission and auto-execute settings, and asks for the web, assets and notes with text markers (`[WEB_SEARCH: ...]`, `[FETCH_URL: ...]`, `[GENERATE_IMAGE: ...]`, `[GENERATE_AUDIO: ...]`, `[WRITE_MEMORY: ...]`). The markers are neither taught to nor read from a model that has the tools.

### Permission system

Execution permissions are categorized and configurable per operation type:

- Create nodes
- Delete nodes
- Modify properties
- Modify scripts
- Change project settings
- Write files
- Delete files
- Remember notes

Each category supports `Allow`, `Ask`, or `Deny`. File deletion defaults to deny; modifying scripts and remembering notes default to ask: a script is code that runs later, and what is inside a script file is not seen by the safety check, and a note becomes part of the prompt of every later conversation. Writing a `.gd` file counts as modifying scripts, as attaching one does.

Generated code cannot reach the assistant's own controls: its classes, the editor's interface tree (`get_tree().root`, `EditorInterface.get_base_control()`, `Engine.get_main_loop()`) and the switches on the controller are rejected by the safety check, and the two controller methods that exist for the headless eval runner (`set_auto_approve`, `configure_provider`) do nothing unless the editor was started by `evals/run_evals.py`.

A reply is only executed if it is complete and is the model's answer. Code is shown but not run when the reply was cut off at the output limit, was withheld by the provider's filter, consists only of reasoning, or was written in PLAN mode. When code fails half-way, the scene and files are put back to the checkpoint taken before it ran, and only then is the model asked to try again.

The table is consulted for every piece of generated code, whatever its risk tier: `Deny` blocks it and `Ask` always shows a confirmation dialog, including when auto-execute is on. With auto-execute off, every execution is confirmed first.

Categories are detected by matching API names in the generated code. This catches ordinary model output; it is not a sandbox, and code written to evade the matching can get past it.

### Context collection

The assistant can build editor-aware context from:

- current scene tree
- selected nodes
- project structure
- currently open script
- inline scene-node mentions
- attached file contents

### Context compression

Long conversations are summarized automatically when token usage grows too large for the current model context window. The implementation keeps recent exchanges intact and compresses older history into a rolling summary. Only the working copy sent to the model is compressed; the full history is kept for display and for saved chats.

Three settings control it (Settings dialog, Context Compression section):

- **Keep recent messages** (2–20, default 6): how many of the latest messages are never compressed
- **Compress at** (30–95%, default 70%): how full the history window may get before compression runs. The window is the model's context length minus the max output tokens and the system prompt
- **AI summary** (default off): ask the model for a summary of the compressed messages in a background request. This costs tokens; when it is off, or the request fails, a local heuristic summary is used instead

Token counts are estimated locally: one token per CJK character, one per four other characters.

If the provider still rejects a request as too long, the oldest messages are dropped and the request is retried once.

### Error monitoring and self-recovery

- Captures editor/runtime errors during AI-triggered execution windows
- Separates AI-caused execution errors from unrelated console noise
- Feeds failures back into the assistant for automatic retry
- Supports up to 3 auto-retry attempts
- Watches the running game: when a played scene stops with runtime errors, the assistant is asked for a fix and the scene is restarted, up to 5 attempts. Can be switched off (Settings > General); with auto-execute off, each fix waits for approval. The error text is passed to the model as untrusted data

### Checkpoints and revert flow

- Creates scene checkpoints before risky modifications
- Stores up to 10 recent checkpoints, each written to disk when taken so built-in sub-resources (materials, shapes) are restored too
- Also snapshots the project's text files (scripts, scenes, resources, config; up to 512 KB each) before each run. Reverting restores files the code changed or deleted and moves files created since to the system trash. Large and binary files are not covered
- Reverting returns the project to that point in time, so it also undoes edits you made to those files after the checkpoint
- Allows reverting through the panel UI

### Chat history

- Saves chat sessions as JSON files under `user://ai_assistant_chats`
- Supports listing, loading, and deleting saved chats

### Asset generation

With the `generate_image` and `generate_speech` tools (or the corresponding text markers, for a model without tools):

- Image generation via OpenAI image API
- Audio generation via OpenAI TTS API

Generation needs an OpenAI API key whatever provider is active, runs beside the conversation, and saves into the path the model named: a new image or audio file inside `res://`. The tool tells the model that the file is not there yet.

### Web search and page fetch

- DuckDuckGo HTML search integration
- Direct URL fetch support, limited to public http(s) addresses and to 3 web requests per user message. The host is checked by name and by every address it resolves to, and the connection is then made to one of exactly those addresses (the name is not resolved a second time, so the answer cannot change between the check and the connection; the TLS certificate is still verified against the name). Redirects are followed one hop at a time through the same checks, so a public page cannot bounce the editor to a local or private address. When a proxy is in use (see [Network proxy](#network-proxy)) the proxy resolves the name, so only the checks on the URL itself remain for fetches that go through it.
- HTML-to-text extraction for prompt augmentation

### Localization

UI strings are localized for English and Chinese.

## Architecture

The agent loop, the conversation, the network client and the UI are separate pieces. The dock only shows what the controller reports and forwards what the user does, so the same controller also runs headless (see [Evals](#evals)).

```text
AIAssistantPanel (dock: widgets, dialogs)
 |- AIChatView             transcript, streaming display, links
 |- AIMentionAutocomplete  "@" completion popup
 |- AIRuntimeWatch         watches the running game, asks for fixes
 `- AIAgentController      the agent loop as a state machine
     |- AISession              history, compression, attachments
     |- AIStreamClient         one streaming request on a worker thread
     |- AICodeRunner           review (syntax, safety, permissions) + execution
     |- AIPromptBuilder        system prompt = knowledge + editor state
     |- AIPlan                 PLAN-mode steps and their checklist file
     |- AIWebRequester         search / fetch asked for by a reply
     |- AIHistorySummarizer    optional model-written history summary
     `- AIAssetService         image / speech generation queue
```

`AIAgentController` is always in exactly one state, and everything in flight belongs to one `Run`:

```text
IDLE --submit / plan step / runtime fix--> STREAMING
STREAMING --reply has code--> (AWAITING_PERMISSION --ok-->) execute --> VERIFYING
STREAMING --reply needs a fix / execution failed--> STREAMING   (at most 3 automatic retries)
VERIFYING --late errors--> STREAMING        VERIFYING --clean--> settle
settle --web marker--> WEB_REQUEST --result--> STREAMING
STREAMING --reply calls tools--> run them (AWAITING_PERMISSION / WEB_REQUEST as needed) --results--> STREAMING
settle --next plan step--> STREAMING        settle --otherwise--> IDLE
any state --stop / error / denied--> IDLE
```

Whether a request may start, whether code may run and what Stop means all follow from the state. The run's bookkeeping (retry count, pending code, markers) is one struct that is reset as a whole when the run ends, so nothing can leak into the next one.

| File | Role |
|------|------|
| `ui/ai_assistant_plugin.*` | Editor plugin entry point |
| `ui/ai_assistant_panel.*` | The dock: layout, user actions, dialogs |
| `ui/ai_details_dialog.*`, `ui/ai_history_list.*` | The window behind a "Details" / "Thinking" link; the list of saved chats |
| `ui/ai_chat_view.*` | Chat transcript, streaming display, clickable links |
| `ui/ai_markdown.*` | Markdown to RichTextLabel BBCode, whole or as a stream; escapes everything the model wrote |
| `ui/ai_mention_autocomplete.*` | `@` completion popup for scene nodes and project files |
| `ui/ai_mention_text_edit.*`, `ui/ai_mention_highlighter.*` | Mention-capable input field with inline chips |
| `ui/ai_settings_dialog.*`, `ui/ai_settings_dialog_model.cpp`, `ui/ai_settings_dialog_layout.cpp` | Settings window (tabs: model, general, permissions, advanced): showing and saving the settings; widgets and captions |
| `providers/ai_provider_settings.*` | The provider part of the settings while it is edited, without widgets: endpoint per provider, typed and removed keys, the output cap to suggest |
| `common/ai_settings.*` | The one place that reads and writes the `ai_assistant/*` settings; tests give it other values |
| `ui/ai_model_selector.*`, `network/ai_model_list_fetcher.*` | Model name field with a pick list; live model list from the provider's API |
| `providers/ai_provider_catalog.h`, `providers/ai_provider_catalog.cpp` | Display names and known models per provider |
| `agent/ai_agent_controller.*`, `agent/ai_agent_controller_replies.cpp` | Agent loop as an explicit state machine; signals instead of UI calls |
| `agent/ai_agent_controller_tools.cpp` | The tool round: permissions, checkpoint, tools that wait (running the game) or need the user |
| `tools/ai_toolbox.*`, `tools/ai_tool_specs.cpp`, `tools/ai_edit_tools.cpp` | The tools: what the model is told about them, the ones that look, the ones that edit (as undoable editor actions) |
| `tools/ai_tool_values.*` | Tool arguments (JSON) to engine values and resources |
| `common/ai_tool_types.h` | Tool specification, tool call, and the assembly of a call from stream fragments |
| `common/ai_text_diff.*` | The lines that change between two versions of a file, for the permission prompt |
| `agent/ai_session.*` | Conversation history, context compression, overflow recovery, attachment de-duplication |
| `agent/ai_chat_store.*` | Saved chats (`user://ai_assistant_chats`) |
| `network/ai_stream_client.*`, `network/ai_stream_protocol.*` | Streaming HTTP client; SSE decoding, UTF-8 boundaries, `<think>` tag splitting |
| `execution/ai_code_runner.*` | Review gate and execution with checkpoints and error monitoring |
| `execution/ai_script_executor.*` | Compiles and runs generated GDScript as an `EditorScript` |
| `execution/ai_safety_policy.*` | What generated code may not contain, checked on its text and its tokens |
| `execution/ai_script_wrapper.*`, `execution/ai_code_fixes.*` | The `EditorScript` wrapper; repairs for mistakes models keep making |
| `execution/ai_permission_manager.*` | Rule-based execution gating |
| `execution/ai_checkpoint_manager.*` | Checkpoint and restore support |
| `execution/ai_error_monitor.*` | Tracks editor/runtime errors around AI execution |
| `agent/ai_runtime_watch.*` | Detects runtime errors of the running game and requests fixes |
| `agent/ai_response_parser.*`, `agent/ai_response_analysis.*` | Code blocks; side-effect markers and the checks on what they carry |
| `network/ai_url_policy.*` | Which addresses a model-supplied URL may point at |
| `assets/ai_asset_request.*` | A generation request and the check on its save path |
| `common/ai_code_text.*`, `common/ai_notice.h` | Summaries and script paths read out of code and error text; notice levels |
| `agent/ai_plan.*` | PLAN-mode steps and their Markdown checklist |
| `network/ai_web_requester.*`, `network/ai_web_fetch.*`, `network/ai_web_search.*` | Web search and page fetch; the fetch runs on a worker thread and connects only to addresses it has checked |
| `assets/ai_asset_service.*`, `assets/ai_image_generator.*`, `assets/ai_audio_generator.*` | Image and speech generation |
| `network/ai_proxy.*`, `network/ai_proxy_os.cpp` | Which HTTP proxy the assistant's requests use (system, none, custom) |
| `network/ai_network_thread.h` | Marks the network worker threads, so their engine errors are not taken for project errors |
| `agent/ai_history_summarizer.*` | Optional model-written summary of compressed history |
| `agent/ai_project_memory.*` | Notes the model asks to remember in `res://godot_ai.md` |
| `prompt/ai_prompt_builder.*`, `prompt/ai_context_collector.*`, `prompt/ai_profiler_collector.*` | System prompt assembly and editor context |
| `prompt/ai_knowledge.*` | Loads the knowledge files (built-in defaults + override directory) |
| `prompt/ai_system_prompt.*`, `prompt/ai_gotchas_index.*`, `prompt/ai_domain_prompts.*`, `prompt/ai_api_doc_loader.*` | Select from the knowledge base what a message needs |
| `providers/ai_provider.*`, `providers/ai_provider_setup.*`, `providers/` | Provider interface, configuration, implementations |
| `providers/ai_credential_store.*` | API key storage outside the settings file |
| `common/ai_localization.h` | UI string localization table |
| `knowledge/` | Prompts, gotchas, API notes and domain guidance as Markdown |
| `tests/` | Unit tests |
| `evals/` | Task set and headless runner for scoring prompt and agent changes |

Provider implementations in `providers/`:

| File | Provider |
|------|----------|
| `openai_compatible_provider.*` | Shared OpenAI chat-completions implementation |
| `openai_provider.*` | OpenAI and OpenAI-compatible endpoints |
| `deepseek_provider.*` | DeepSeek |
| `glm_provider.*` | Zhipu AI GLM |
| `anthropic_provider.*` | Anthropic Claude |
| `gemini_provider.*` | Google Gemini |

## Knowledge files

Everything the assistant is told that is not code lives in `knowledge/` as plain Markdown:

| Path | Content |
|------|---------|
| `system_prompt.md` | The base system prompt. `{{project_instructions}}` marks where the project's `godot_ai.md` goes |
| `prompts/<id>.md` | Short templates with `{{name}}` placeholders: mode instructions, the retry messages, plan step, runtime fix, web results, summary, preset buttons |
| `domains/<id>.md` | Guidance for one domain. Front matter lists the keywords that select it (`match: stem` or `match: word`) and its `order` in the prompt |
| `gotchas.md` | One entry per `<!-- keywords: a, b -->` line; the text that follows is injected when a message mentions those keywords |
| `api_docs.md` | One `## ClassName` section per class, injected when the class is named |
| `glossary.md` | Terms a message may use, in any language, and the engine names they stand for (`跳跃 => CharacterBody2D, move_and_slide`). Lets a message that names no class still select gotchas and API notes |

These files are embedded into the editor binary at build time as the defaults. To change them **without rebuilding**, point the assistant at a directory with the same layout:

- the editor setting `ai_assistant/knowledge_dir`, or
- the environment variable `GODOT_AI_KNOWLEDGE_DIR`, or
- the folder `ai_assistant/knowledge` in the editor's config directory (used automatically if it exists).

A file there replaces the built-in file with the same relative path; additional files under `domains/` and `gotchas/` are added. The directory is checked before every request, so an edit takes effect with the next message. A file that does not parse is reported in the editor output and skipped; the built-in content stays in place.

While developing prompts, set `ai_assistant/knowledge_dir` to this repository's `knowledge/` folder and edit the files in place. Use the eval set to see whether a change helped.

## API keys

Keys are not stored in the editor settings file (`editor_settings-*.tres` is plain text and tends to get copied, synced and attached to bug reports). For each provider the key is looked up in this order:

1. The environment variable `GODOT_AI_API_KEY_<PROVIDER>` (for example `GODOT_AI_API_KEY_DEEPSEEK`), then the vendor's usual variable: `ANTHROPIC_API_KEY`, `OPENAI_API_KEY`, `GEMINI_API_KEY`, `DEEPSEEK_API_KEY`, `ZHIPUAI_API_KEY`.
2. The credential store, `ai_assistant/credentials.json` in the editor's config directory. Keys typed into the settings dialog go here.

On Windows each stored key is sealed with DPAPI, so it can only be read by the same Windows user on the same machine. On other platforms it is AES-encrypted with a key derived from the machine id: that keeps the key out of sight and makes a copied file useless elsewhere, but it is obfuscation rather than a keychain — prefer environment variables there.

Keys found in the settings file by an older version are moved into the store the first time the panel loads, and the settings entries are blanked. The settings dialog never shows a stored key: the field is empty and only takes a new key to replace the stored one.

## Layers

The folders form a stack, lowest first:

```text
common < providers < network < execution < prompt < assets < tools < agent < ui
```

A folder includes headers from its own folder and from the ones before it, never from one after it. The editor's settings are reached through `common/ai_settings.cpp` only (the `ui` folder also registers them with the editor). `evals/check_layers.py` checks both rules and names every `#include` that breaks one:

```bash
python evals/check_layers.py
```

## Tests

Unit tests cover the logic that does not need a running editor: the response parser, side-effect markers, URL and path policy, the safety policy and risk tiers, gotcha / domain / API-doc selection, the knowledge file formats and overrides, Markdown rendering, the settings and the provider settings model, the tools (on a scene built in the test) and each provider's tool-call format, history compression and overflow recovery, SSE decoding, the providers' request and response handling, and the credential store.

```bash
scons platform=windows target=editor tests=yes
bin/godot.windows.editor.x86_64.console.exe --test --test-case="*AIAssistant*"
```

Adding `extra_suffix=tests` to the build keeps the test build's objects and binary apart from your normal editor build, so switching between the two does not trigger full rebuilds.

### Continuous integration

`.github/workflows/ci.yml` checks out the engine with this module in it, builds the editor with tests on Windows and runs the unit tests, the layering check and the eval set against canned replies — none of which needs an API key. `GODOT_REF` in the workflow is the engine revision built against. Releases are for now built locally and uploaded by hand; the workflow's release job only runs when started manually.

## Evals

Whether the assistant is any good is decided by what the model produces, and that changes with every edit to a prompt or to the retry logic. `evals/` holds a set of standard tasks (scene building, scripts, UI, 3D, gameplay, ASK / PLAN modes, safety) and a runner that scores them without a human in the loop. See [evals/README.md](evals/README.md).

```bash
# The harness and the agent loop themselves, against canned replies (no API key, ~2 minutes):
python evals/run_evals.py --godot bin/godot.windows.editor.x86_64.console.exe --mock

# A real model; compare a prompt change against an earlier run:
python evals/run_evals.py --godot <editor> --provider deepseek --out before.json
python evals/run_evals.py --godot <editor> --provider deepseek --knowledge-dir knowledge --baseline before.json
```

## Build and Installation

If you would rather not compile, [Releases](https://github.com/spardanviro/Godot_AI/releases) has the Windows editor with this module built in: unzip and run. Other platforms have to be built from source for now.

### 1. Place the module in the Godot source tree

```text
godot/modules/ai_assistant/
```

This repository is the module directory. Clone or copy it directly under `modules/`.

### 2. Build the editor

```bash
scons platform=windows target=editor
```

Adjust `platform`, `arch`, and other SCons flags for your environment. The module is guarded by `TOOLS_ENABLED` and only compiled into editor builds.

### 3. Launch the custom editor build

After compiling, open Godot using the built editor executable. The AI panel appears in the right dock.

## Network proxy

Everything the assistant sends — chat requests, the model list, summaries, image and speech generation, web search and page fetch — goes through one proxy choice (Settings > Advanced):

- **Follow the system** (default): the editor's own `network/http_proxy` setting if set, else `HTTPS_PROXY` / `HTTP_PROXY`, else on Windows the proxy configured for the user account. Proxy auto-configuration scripts are not evaluated.
- **None**: connect directly.
- **Custom**: `host:port`.

Only HTTP proxies are supported (no SOCKS, no proxy credentials). Local addresses — `localhost`, loopback and private-network literals — are always reached directly, so a local model server keeps working.

A page fetch through a proxy cannot check which address a name resolves to, because the proxy does the resolving; the checks on the URL (scheme, local names, address literals, every redirect hop) still apply. Set the proxy to **None** if that matters more to you than reaching sites the proxy is needed for.

## Configuration

Settings are stored in `EditorSettings` under the `ai_assistant/*` namespace. API keys are not among them; see [API keys](#api-keys).

| Key | Description |
|-----|-------------|
| `ai_assistant/provider` | Active provider (`anthropic`, `openai`, `gemini`, `deepseek`, `glm`) |
| `ai_assistant/model` | Model name |
| `ai_assistant/api_endpoint` | Custom endpoint override |
| `ai_assistant/custom_max_tokens` | Use the max tokens below instead of the model's recommendation (default: off) |
| `ai_assistant/max_tokens` | Max response tokens, used when the setting above is on |
| `ai_assistant/thinking` | Thinking setting: empty for the model's default, `off`, one of the model's levels, or a budget in tokens |
| `ai_assistant/auto_update_model_specs` | Download current model facts from models.dev when the copy is a week old (default: true) |
| `ai_assistant/custom_temperature` | Send the temperature below instead of leaving it to the model (default: off) |
| `ai_assistant/temperature` | Sampling temperature, used when the setting above is on (default: 0.7) |
| `ai_assistant/send_on_enter` | Send on Enter key (default: true) |
| `ai_assistant/autorun` | Auto-execute generated code (default: true) |
| `ai_assistant/use_tools` | Let the model work with tools (default: true) |
| `ai_assistant/restore_last_chat` | Reload the most recent chat on editor startup (default: true) |
| `ai_assistant/runtime_fix` | Fix script errors of the running game automatically (default: true) |
| `ai_assistant/compression_keep_recent` | Recent messages never compressed (default: 6) |
| `ai_assistant/compression_threshold_pct` | Fraction of the history window that triggers compression (default: 0.7) |
| `ai_assistant/compression_use_ai_summary` | Summarize compressed messages with the model (default: false) |
| `ai_assistant/language` | UI language (`en` or `zh`) |
| `ai_assistant/knowledge_dir` | Folder of knowledge files that override the built-in ones (default: empty) |
| `ai_assistant/proxy_mode` | `system` (default), `off` or `custom` |
| `ai_assistant/proxy_address` | `host:port` of the HTTP proxy for `custom` |
| `ai_assistant/image_model` | OpenAI image model (default: empty = `gpt-image-1`) |
| `ai_assistant/speech_model` | OpenAI speech model (default: empty = `gpt-4o-mini-tts`) |
| `ai_assistant/permissions/*` | Per-category execution permissions |

## Project-local AI instructions

Create `res://godot_ai.md` in your project to define project-specific instructions. The assistant loads this file and appends it to the system prompt automatically.

## Usage Flow

1. Open the settings dialog from the AI panel.
2. Choose a provider and enter the API key.
3. Optionally override the endpoint and model.
4. Choose whether Enter sends immediately and whether generated code auto-runs.
5. Configure permission levels for sensitive operations.
6. Start using the panel in `ASK`, `AGENT`, or `PLAN` mode.
7. Optionally enrich the next request by:
   - attaching a file with the `+` button
   - mentioning scene nodes or project files with `@`
   - dragging scene nodes, files, or folders into the input field

## Project Layout

```text
ai_assistant/
|- ui/            dock, chat view, Markdown rendering, mentions, settings dialog
|- agent/         agent loop, session, saved chats, plan, reply parsing and analysis
|- execution/     code review and execution, safety policy, permissions, checkpoints, error monitoring
|- prompt/        system prompt assembly, editor context, knowledge loading and selection
|- network/       streaming client, web search and fetch, proxy, model list
|- providers/     provider interface, implementations, catalog, API key storage
|- assets/        image and speech generation
|- tools/         the tools a model calls: look, edit, and their argument conversion
|- common/        settings access, logging, localization, notice levels, code text helpers
|- knowledge/     prompts, gotchas, API notes, domain guidance (Markdown)
|- tests/         unit tests (doctest)
|- evals/         eval tasks, headless runner, driver script
|- ai_knowledge_builders.py   embeds knowledge/ into the build
|- register_types.*
|- SCsub
`- config.py
```

## Current Limitations

- Requires user-provided API keys for all LLM providers.
- Web search is implemented via HTML scraping; provider pages may require maintenance.
- Asset generation currently uses OpenAI-specific endpoints and needs an OpenAI API key in Settings, whichever provider is active. Generated assets can only be saved under `res://`.
- The dock itself (layout, dialogs) is not covered by automated tests; the agent loop behind it is, through the eval runner's mock mode.
- On platforms other than Windows the credential store is obfuscation, not OS-level protection.
- Prebuilt packages exist for Windows only; builds on Linux and macOS have not been verified.
- Tool calling has been run through the full eval set with DeepSeek and with Claude (over both the OpenAI-compatible and the Anthropic wire formats); GPT models and Gemini have not been tested live.

## License

Licensed under the Apache License 2.0. See [LICENSE](LICENSE).

This applies to the code in this repository. Godot engine upstream is MIT-licensed.
