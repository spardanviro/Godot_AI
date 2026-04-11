# Godot_AI

AI assistant support inside the Godot editor as a native `ai_assistant` module for Godot 4.5.x.

This project adds a docked AI panel to the editor, supports multiple LLM providers, and can execute AI-generated editor actions through `EditorScript` workflows. It is built for scene editing, script generation, project setup, file-aware prompting, asset generation, and debugging assistance directly inside Godot.

## Overview

`ai_assistant` is a native C++ Godot module. It is not a GDExtension and not a regular addon distributed as project files. It is intended to live under the Godot engine source tree and be compiled into the editor build.

Once enabled, the module registers:

- `AIAssistantPlugin` as an editor plugin
- `AIAssistantPanel` as the main dock UI
- provider integrations for Anthropic, OpenAI-compatible APIs, and Gemini
- helper systems for response parsing, script execution, context collection, permissions, checkpoints, error monitoring, web search, asset generation, and mention-aware input

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
- Use the scene tree right-click action `Mention in AI Assistant`
- Mention chips are rendered inline and expanded into structured node context before the request is sent
- Mention placeholders are atomic single-character tokens, so backspace removes them cleanly

### File attachments

- Attach project files for the next request through an editor file picker
- Attached file contents are appended to the outgoing prompt as explicit context
- Supported attachment filters currently include:
  - Markdown / diagram: `*.md`, `*.mmd`, `*.mermaid`
  - Text / data: `*.txt`, `*.json`, `*.yaml`, `*.yml`, `*.toml`, `*.csv`
  - GDScript: `*.gd`, `*.gdscript`

### Provider support

- Anthropic
- OpenAI
- Gemini

The OpenAI provider path can also be pointed at compatible custom endpoints through the endpoint setting.

The settings dialog can auto-fetch model lists from provider APIs and select from the fetched models. It also stores separate API keys per provider and swaps them automatically when you change providers.

Current built-in defaults:

- Anthropic: `claude-opus-4-6`
- OpenAI: `gpt-5.4`
- Gemini: `gemini-3.0-pro`

### Code generation and execution

- Parses AI replies and extracts executable GDScript blocks
- Wraps generated code as `EditorScript`
- Executes code in the editor context
- Supports compile-only validation before execution
- Uses a basic dangerous-call blocklist before execution
- Integrates permission checks for sensitive operations
- Skips code execution in `ASK` mode
- If a streamed response is manually stopped, partial code is preserved in history but not executed

Blocked API patterns currently include:

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

Each category supports:

- `Allow`
- `Ask`
- `Deny`

The default design is safety-oriented. File deletion defaults to deny in the settings UI.

### Context collection

The assistant can build editor-aware context from:

- current scene tree
- selected nodes
- project structure
- currently open script
- inline scene-node mentions
- attached file contents

This context is injected into prompts so the assistant can generate editor-relevant changes instead of generic code.

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

- image generation via OpenAI image API
- audio generation via OpenAI TTS API

Generated assets can be written into project paths such as:

- `res://ai_generated_image.png`
- `res://ai_generated_audio.mp3`

### Web search and page fetch

- DuckDuckGo HTML search integration
- direct URL fetch support
- HTML-to-text extraction for prompt augmentation

This is intended for “latest docs / current info” style requests from inside the editor.

### UI-focused prompting

The module contains a specialized UI agent helper for Control-node and layout-oriented requests, and the system prompt strongly pushes scene-first and editor-first workflows rather than overbuilding logic in code.

### Localization

UI strings are localized for:

- English
- Chinese

## Architecture

Core components:

- [`AIAssistantPanel`](C:\Users\Nero\Desktop\Godot\godot-4.5.2-stable\modules\ai_assistant\ai_assistant_panel.h): main editor dock, request flow, mentions, attachments, history, streaming, and asset/web handling
- [`AIAssistantPlugin`](C:\Users\Nero\Desktop\Godot\godot-4.5.2-stable\modules\ai_assistant\ai_assistant_plugin.h): editor plugin entry point and scene-tree context-menu hook
- [`AISettingsDialog`](C:\Users\Nero\Desktop\Godot\godot-4.5.2-stable\modules\ai_assistant\ai_settings_dialog.h): provider and behavior configuration UI
- [`AIMentionTextEdit`](C:\Users\Nero\Desktop\Godot\godot-4.5.2-stable\modules\ai_assistant\ai_mention_text_edit.h): mention-capable chat input with inline chips, drag-and-drop, and mention expansion
- [`AIMentionHighlighter`](C:\Users\Nero\Desktop\Godot\godot-4.5.2-stable\modules\ai_assistant\ai_mention_highlighter.h): syntax highlighter that hides mention placeholder codepoints behind rendered chips
- [`AIProvider`](C:\Users\Nero\Desktop\Godot\godot-4.5.2-stable\modules\ai_assistant\ai_provider.h): abstract provider interface for requests, streaming, model listing, and response parsing
- [`AIResponseParser`](C:\Users\Nero\Desktop\Godot\godot-4.5.2-stable\modules\ai_assistant\ai_response_parser.h): extracts text and code blocks from model replies
- [`AIScriptExecutor`](C:\Users\Nero\Desktop\Godot\godot-4.5.2-stable\modules\ai_assistant\ai_script_executor.h): wraps and runs generated GDScript safely in editor context
- [`AIContextCollector`](C:\Users\Nero\Desktop\Godot\godot-4.5.2-stable\modules\ai_assistant\ai_context_collector.h): collects scene, selection, script, and project information
- [`AIErrorMonitor`](C:\Users\Nero\Desktop\Godot\godot-4.5.2-stable\modules\ai_assistant\ai_error_monitor.h): tracks editor/runtime errors around AI execution
- [`AICheckpointManager`](C:\Users\Nero\Desktop\Godot\godot-4.5.2-stable\modules\ai_assistant\ai_checkpoint_manager.h): checkpoint and restore support
- [`AIPermissionManager`](C:\Users\Nero\Desktop\Godot\godot-4.5.2-stable\modules\ai_assistant\ai_permission_manager.h): rule-based execution gating
- [`AIWebSearch`](C:\Users\Nero\Desktop\Godot\godot-4.5.2-stable\modules\ai_assistant\ai_web_search.h): search and page text extraction
- [`AIImageGenerator`](C:\Users\Nero\Desktop\Godot\godot-4.5.2-stable\modules\ai_assistant\ai_image_generator.h): image generation helpers
- [`AIAudioGenerator`](C:\Users\Nero\Desktop\Godot\godot-4.5.2-stable\modules\ai_assistant\ai_audio_generator.h): audio generation helpers
- [`AIUIAgent`](C:\Users\Nero\Desktop\Godot\godot-4.5.2-stable\modules\ai_assistant\ai_ui_agent.h): UI-specific prompting helper

Provider implementations:

- [`providers/anthropic_provider.*`](C:\Users\Nero\Desktop\Godot\godot-4.5.2-stable\modules\ai_assistant\providers\anthropic_provider.h)
- [`providers/openai_provider.*`](C:\Users\Nero\Desktop\Godot\godot-4.5.2-stable\modules\ai_assistant\providers\openai_provider.h)
- [`providers/gemini_provider.*`](C:\Users\Nero\Desktop\Godot\godot-4.5.2-stable\modules\ai_assistant\providers\gemini_provider.h)

Module registration is done in [`register_types.cpp`](C:\Users\Nero\Desktop\Godot\godot-4.5.2-stable\modules\ai_assistant\register_types.cpp).

## Build and Installation

### 1. Place the module in the Godot source tree

Expected location:

```text
godot/modules/ai_assistant
```

### 2. Build the editor

This module is editor-oriented and guarded by `TOOLS_ENABLED` in the plugin-facing pieces, so it is intended for editor builds.

Typical example:

```bash
scons platform=windows target=editor
```

Adjust `platform`, `arch`, and any other SCons flags to match your environment.

### 3. Launch the custom editor build

After compiling, open Godot using the built editor executable. The AI panel should appear in the right dock.

## Configuration

Settings are stored in `EditorSettings` under the `ai_assistant/*` namespace.

Known keys include:

- `ai_assistant/provider`
- `ai_assistant/api_key`
- `ai_assistant/api_key_anthropic`
- `ai_assistant/api_key_openai`
- `ai_assistant/api_key_gemini`
- `ai_assistant/model`
- `ai_assistant/api_endpoint`
- `ai_assistant/max_tokens`
- `ai_assistant/temperature`
- `ai_assistant/send_on_enter`
- `ai_assistant/autorun`
- `ai_assistant/language`
- `ai_assistant/permissions/create_nodes`
- `ai_assistant/permissions/delete_nodes`
- `ai_assistant/permissions/modify_properties`
- `ai_assistant/permissions/modify_scripts`
- `ai_assistant/permissions/project_settings`
- `ai_assistant/permissions/file_write`
- `ai_assistant/permissions/file_delete`

Default behavior in the current codebase:

- default provider: `anthropic`
- default max tokens: `4096`
- default temperature: `0.7`
- default `send_on_enter`: `true`
- default `autorun`: `true`
- default language: `en`

## Usage Flow

1. Open the settings dialog from the AI panel.
2. Choose a provider.
3. Enter the API key for that provider.
4. Optionally override the endpoint and model.
5. Choose whether Enter sends immediately and whether generated code auto-runs.
6. Configure permission levels for sensitive operations.
7. Start using the panel in `ASK`, `AGENT`, or `PLAN` mode.
8. Optionally enrich the next request by:
   - attaching a file with the `+` button
   - mentioning scene nodes with `@`
   - dragging scene nodes into the input field
   - using `Mention in AI Assistant` from the scene tree context menu

Typical use cases:

- ask the assistant to inspect or explain the current scene
- generate nodes and configure them in the open scene
- create or modify scripts
- analyze console errors
- gather profiler data and suggest optimizations
- mention scene nodes as explicit prompt anchors
- attach design docs, data files, or scripts to the next request
- generate image or audio assets into `res://`
- search the web for up-to-date docs or references

## Prompting Model Behavior

The base system prompt is defined in [`ai_system_prompt.cpp`](C:\Users\Nero\Desktop\Godot\godot-4.5.2-stable\modules\ai_assistant\ai_system_prompt.cpp).

Important design choices:

- strong bias toward editor-first and scene-first workflows
- prefers built-in Godot nodes and editor tooling over excessive generated code
- supports script creation, project settings updates, asset generation markers, and web search markers
- loads extra project-specific instructions from `res://godot_ai.md` if present

This gives projects a way to define local AI instructions without modifying the module itself.

## Project Layout

```text
ai_assistant/
|- providers/
|  |- anthropic_provider.*
|  |- gemini_provider.*
|  `- openai_provider.*
|- ai_assistant_panel.*
|- ai_assistant_plugin.*
|- ai_audio_generator.*
|- ai_checkpoint_manager.*
|- ai_context_collector.*
|- ai_error_monitor.*
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

- This repository contains the module source. Release binaries may exist separately, but the repo itself is source-first.
- The module depends on live third-party API access and user-provided API keys.
- Web search is implemented via HTML scraping flow, so providers or page formats may require maintenance over time.
- Asset generation currently uses OpenAI-specific endpoints in the panel implementation.
- The repository does not yet include automated CI, automated tests, or cross-platform packaging workflows.

## License

This repository is licensed under Apache License 2.0. See [`LICENSE`](C:\Users\Nero\Desktop\Godot\godot-4.5.2-stable\modules\ai_assistant\LICENSE).

This applies to the code in this repository. Godot engine upstream remains under its own MIT license.
