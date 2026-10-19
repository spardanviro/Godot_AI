"""Refreshes data/model_specs.json, the model facts built into the editor.

    python update_model_specs.py              # downloads https://models.dev/api.json
    python update_model_specs.py api.json     # or reads a copy of it

The facts are what models.dev lists for each model of the providers the
assistant speaks to: context window, output limit, whether a temperature is
accepted, and which thinking settings exist. The editor refreshes the same data
on its own (AIModelSpecsUpdater) and keeps it in its config folder; the file
written here is what it falls back to before that, or without a connection.
The editor filters the download with the same rules (AIModelSpecs::filter), so
both hold the same shape.
"""

from __future__ import annotations

import json
import sys
import urllib.request
from pathlib import Path

SOURCE_URL = "https://models.dev/api.json"
# models.dev provider ids, for the assistant's anthropic, openai, gemini, deepseek and glm.
PROVIDERS = ["anthropic", "openai", "google", "deepseek", "zhipuai", "zhipuai-coding-plan"]
MODEL_KEYS = ["limit", "temperature", "reasoning_options", "release_date", "status", "tool_call", "family"]
LIMIT_KEYS = ["context", "output"]
OUTPUT = Path(__file__).resolve().parent / "data" / "model_specs.json"


def filter_specs(data: dict) -> dict:
    kept = {}
    for provider_id in PROVIDERS:
        provider = data.get(provider_id)
        if not isinstance(provider, dict) or not isinstance(provider.get("models"), dict):
            continue
        models = {}
        for model_id, model in provider["models"].items():
            if not isinstance(model, dict):
                continue
            entry = {key: model[key] for key in MODEL_KEYS if key in model}
            if isinstance(entry.get("limit"), dict):
                entry["limit"] = {key: entry["limit"][key] for key in LIMIT_KEYS if key in entry["limit"]}
            models[model_id] = entry
        kept[provider_id] = {"models": models}
    return kept


def main() -> int:
    if len(sys.argv) > 1:
        text = Path(sys.argv[1]).read_text(encoding="utf-8")
    else:
        with urllib.request.urlopen(SOURCE_URL, timeout=60) as response:
            text = response.read().decode("utf-8")
    kept = filter_specs(json.loads(text))
    OUTPUT.parent.mkdir(exist_ok=True)
    OUTPUT.write_text(json.dumps(kept, ensure_ascii=False, sort_keys=True, indent=0) + "\n", encoding="utf-8", newline="\n")
    count = sum(len(provider["models"]) for provider in kept.values())
    print(f"{OUTPUT.name}: {count} models from {len(kept)} providers")
    return 0


if __name__ == "__main__":
    sys.exit(main())
