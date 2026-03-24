# AI Assistant Module for Godot

`ai_assistant` is a custom Godot module that adds AI-assisted tooling inside the editor.

## Current Scope

- Multi-provider support for OpenAI, Gemini, and Anthropic
- Assistant panel and settings UI
- Context collection, profiling, checkpoint, and error-monitor helpers
- Image, audio, and web-related assistant features

## Project Structure

- `providers/`: provider-specific integrations
- `SCsub`, `register_types.*`: Godot module registration/build integration
- `ai_*`: module features and editor UI components

## Build Notes

This module is intended to live under:

```text
godot/modules/ai_assistant
```

Then build Godot with the editor target enabled so the module is compiled into the editor build.

## Status

This repository is an extracted standalone copy of the module source, prepared for version control and GitHub publishing.
