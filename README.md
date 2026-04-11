# Godot AI Assistant

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
- Auto-resizing multiline input field
- High-frequency preset actions:
  - `Fix Errors`
  - `Performance`

### Assistant modes

- `ASK`: conversational mode, no code execution
- `AGENT`: action-oriented mode with execution flow
- `PLAN`: planning-first mode before execution

### Scene node mentions and editor-aware input

- Type `@` in the input box to open scene-node autocomplete
- Drag scene nodes into the input field to insert mention chips
- Use the `@` toolbar button to mention the current scene selection
- Mention chips are rendered inline and expanded into structured node context before the request is sent
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

The settings dialog can auto-fetch model lists from provider APIs and select from the fetched models. It stores separate API keys per provider and swaps them automatically when you change providers.

### On-demand API reference injection (`AIAPIDocLoader`)

The system prompt stays lean. Instead of statically including all Godot API documentation, `AIAPIDocLoader` detects Godot class names mentioned in the user's message (word-boundary matched) and injects compact API references for only those classes. Covers 50+ commonly-used classes including `CharacterBody3D`, `NavigationAgent3D`, `AnimationPlayer`, `RigidBody3D`, and more.

### On-demand gotchas injection (`AIGotchasIndex`)

401 Godot-specific gotchas and pitfalls are stored in a keyword-indexed table. At request time, only entries whose keywords match the user's message are injected (capped at ~3000 chars). This replaces a previous 3100-line static section that was injected on every request regardless of relevance.

Token savings per request: ~90% reduction in gotcha-related context overhead.

### Domain-specific prompting (`AIDomainPrompts`)

Specialized system prompt fragments for domain-focused requests (UI/Control layout, physics, navigation, shaders, etc.) are injected on demand alongside the base system prompt.

### Code generation and execution

- Parses AI replies and extracts executable GDScript blocks
- Wraps generated code as `EditorScript`
- Executes code in the editor context
- Supports compile-only validation before execution
- Uses a basic dangerous-call blocklist before execution
- Integrates permission checks for sensitive operations
- Skips code execution in `ASK` mode
- If a streamed response is manually stopped, partial code is preserved in history but not executed

Blocked API patterns:

- `OS.execute`
- `OS.shell_open`
- `OS.kill`
- `OS.create_process`
- `.shell_execute`

### Permission system

Execution permissions are categorized and configurable per operation type:

- Create nodes
- Delete nodes
- Modify properties
- Modify scripts
- Change project settings
- Write files
- Delete files

Each category supports `Allow`, `Ask`, or `Deny`. File deletion defaults to deny.

### Context collection

The assistant can build editor-aware context from:

- current scene tree
- selected nodes
- project structure
- currently open script
- inline scene-node mentions
- attached file contents

### Context compression

Long conversations are summarized automatically when token usage grows too large for the current model context window. The implementation keeps recent exchanges intact and compresses older history into a rolling summary.

### Error monitoring and self-recovery

- Captures editor/runtime errors during AI-triggered execution windows
- Separates AI-caused execution errors from unrelated console noise
- Feeds failures back into the assistant for automatic retry
- Supports up to 3 auto-retry attempts

### Checkpoints and revert flow

- Creates scene checkpoints before risky modifications
- Stores multiple recent checkpoints
- Allows reverting through the panel UI

### Chat history

- Saves chat sessions as JSON files under `user://ai_assistant_chats`
- Supports listing, loading, and deleting saved chats

### Asset generation

The assistant supports non-code markers that are intercepted by the module:

- Image generation via OpenAI image API
- Audio generation via OpenAI TTS API

Generated assets are written into project paths such as `res://ai_generated_image.png` and `res://ai_generated_audio.mp3`.

### Web search and page fetch

- DuckDuckGo HTML search integration
- Direct URL fetch support
- HTML-to-text extraction for prompt augmentation

### Localization

UI strings are localized for English and Chinese.

## Architecture

Core components:

| File | Role |
|------|------|
| `ai_assistant_panel.*` | Main editor dock, request flow, mentions, attachments, history, streaming |
| `ai_assistant_plugin.*` | Editor plugin entry point |
| `ai_settings_dialog.*` | Provider and behavior configuration UI |
| `ai_mention_text_edit.*` | Mention-capable chat input with inline chips and drag-and-drop |
| `ai_mention_highlighter.*` | Syntax highlighter that renders mention chips |
| `ai_provider.*` | Abstract provider interface |
| `ai_response_parser.*` | Extracts text and code blocks from model replies |
| `ai_script_executor.*` | Wraps and runs generated GDScript in editor context |
| `ai_context_collector.*` | Collects scene, selection, script, and project information |
| `ai_error_monitor.*` | Tracks editor/runtime errors around AI execution |
| `ai_checkpoint_manager.*` | Checkpoint and restore support |
| `ai_permission_manager.*` | Rule-based execution gating |
| `ai_web_search.*` | Search and page text extraction |
| `ai_image_generator.*` | Image generation helpers |
| `ai_audio_generator.*` | Audio generation helpers |
| `ai_ui_agent.*` | UI-specific prompting helper |
| `ai_profiler_collector.*` | Profiler data collection for performance prompts |
| `ai_system_prompt.*` | Base system prompt builder (~1150 lines, lean) |
| `ai_api_doc_loader.*` | On-demand Godot API reference injection |
| `ai_gotchas_index.*` | On-demand gotchas injection (401 entries, keyword-indexed) |
| `ai_domain_prompts.*` | Domain-specific prompt fragments |
| `ai_localization.h` | UI string localization table |

Provider implementations in `providers/`:

| File | Provider |
|------|----------|
| `anthropic_provider.*` | Anthropic Claude |
| `openai_provider.*` | OpenAI (also covers DeepSeek-compatible) |
| `gemini_provider.*` | Google Gemini |
| `deepseek_provider.*` | DeepSeek |
| `glm_provider.*` | Zhipu AI GLM |

## Build and Installation

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

## Configuration

Settings are stored in `EditorSettings` under the `ai_assistant/*` namespace.

| Key | Description |
|-----|-------------|
| `ai_assistant/provider` | Active provider (`anthropic`, `openai`, `gemini`, `deepseek`, `glm`) |
| `ai_assistant/api_key_anthropic` | Anthropic API key |
| `ai_assistant/api_key_openai` | OpenAI API key |
| `ai_assistant/api_key_gemini` | Gemini API key |
| `ai_assistant/api_key_deepseek` | DeepSeek API key |
| `ai_assistant/api_key_glm` | GLM API key |
| `ai_assistant/model` | Model name |
| `ai_assistant/api_endpoint` | Custom endpoint override |
| `ai_assistant/max_tokens` | Max response tokens (default: 4096) |
| `ai_assistant/temperature` | Sampling temperature (default: 0.7) |
| `ai_assistant/send_on_enter` | Send on Enter key (default: true) |
| `ai_assistant/autorun` | Auto-execute generated code (default: true) |
| `ai_assistant/language` | UI language (`en` or `zh`) |
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
   - mentioning scene nodes with `@`
   - dragging scene nodes into the input field

## Project Layout

```text
ai_assistant/
|- providers/
|  |- anthropic_provider.*
|  |- openai_provider.*
|  |- gemini_provider.*
|  |- deepseek_provider.*
|  `- glm_provider.*
|- ai_api_doc_loader.*
|- ai_assistant_panel.*
|- ai_assistant_plugin.*
|- ai_audio_generator.*
|- ai_checkpoint_manager.*
|- ai_context_collector.*
|- ai_domain_prompts.*
|- ai_error_monitor.*
|- ai_gotchas_index.*
|- ai_image_generator.*
|- ai_localization.h
|- ai_mention_highlighter.*
|- ai_mention_text_edit.*
|- ai_permission_manager.*
|- ai_profiler_collector.*
|- ai_provider.*
|- ai_response_parser.*
|- ai_script_executor.*
|- ai_settings_dialog.*
|- ai_system_prompt.*
|- ai_ui_agent.*
|- ai_web_search.*
|- register_types.*
|- SCsub
`- config.py
```

## Current Limitations

- Requires user-provided API keys for all LLM providers.
- Web search is implemented via HTML scraping; provider pages may require maintenance.
- Asset generation currently uses OpenAI-specific endpoints.
- No automated CI or cross-platform packaging workflows yet.

## License

Licensed under the Apache License 2.0. See [LICENSE](LICENSE).

This applies to the code in this repository. Godot engine upstream is MIT-licensed.
