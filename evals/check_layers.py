"""Checks the module's layering.

    python evals/check_layers.py

Two rules, both about #include lines:

1. Each folder may include headers from its own folder and from the folders
   listed before it in LAYERS, never from one listed after it.
2. The editor's settings are reached through common/ai_settings.cpp only. The
   ui folder may include the header too: it registers the settings and their
   defaults with the editor.

Exits non-zero and names every offending #include otherwise. tests/ and the
module root may include anything.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

# Lowest first.
LAYERS = ["common", "providers", "network", "execution", "prompt", "assets", "agent", "ui"]

MODULE_INCLUDE = re.compile(r'^\s*#include "\.\./(\w+)/([\w.]+)"', re.MULTILINE)
EDITOR_SETTINGS_INCLUDE = '#include "editor/settings/editor_settings.h"'
MAY_INCLUDE_EDITOR_SETTINGS = {"common/ai_settings.cpp"}
FOLDERS_THAT_MAY_INCLUDE_EDITOR_SETTINGS = {"ui"}


def find_violations(root: Path) -> list[str]:
    rank = {name: index for index, name in enumerate(LAYERS)}
    violations = []
    for folder in LAYERS:
        for path in sorted((root / folder).glob("*")):
            if path.suffix not in (".h", ".cpp"):
                continue
            name = f"{folder}/{path.name}"
            text = path.read_text(encoding="utf-8")
            for match in MODULE_INCLUDE.finditer(text):
                target = match.group(1)
                if target not in rank:
                    violations.append(f"{name}: includes {target}/{match.group(2)}, which is not a known layer")
                elif rank[target] > rank[folder]:
                    violations.append(f"{name}: includes {target}/{match.group(2)} ({folder} is below {target})")
            reads_editor_settings = EDITOR_SETTINGS_INCLUDE in text
            allowed = name in MAY_INCLUDE_EDITOR_SETTINGS or folder in FOLDERS_THAT_MAY_INCLUDE_EDITOR_SETTINGS
            if reads_editor_settings and not allowed:
                violations.append(f"{name}: includes the editor settings; read them through AISettings instead")
    return violations


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    missing = [name for name in LAYERS if not (root / name).is_dir()]
    if missing:
        print("Missing folders: " + ", ".join(missing))
        return 1
    violations = find_violations(root)
    for line in violations:
        print(line)
    print(f"{len(violations)} violation(s)." if violations else "Layers OK: " + " < ".join(LAYERS))
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
