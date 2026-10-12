"""Cross-checks `Class.member` references in the knowledge files against the engine's class docs."""

import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

engine = Path(sys.argv[1])
knowledge = Path(sys.argv[2])

classes: dict[str, dict] = {}
for xml_path in list((engine / "doc" / "classes").glob("*.xml")) + list(engine.glob("modules/*/doc_classes/*.xml")):
    try:
        root = ET.parse(xml_path).getroot()
    except ET.ParseError:
        continue
    names = set()
    for tag in ("methods/method", "members/member", "signals/signal", "constants/constant", "constructors/constructor", "theme_items/theme_item", "annotations/annotation"):
        for element in root.findall(tag):
            names.add(element.get("name", "").lstrip("@"))
    for element in root.findall("constants/constant"):
        if element.get("enum"):
            names.add(element.get("enum").split(".")[-1])
    classes[root.get("name")] = {"names": names, "inherits": root.get("inherits")}


def has_member(class_name: str, member: str) -> bool:
    while class_name in classes:
        if member in classes[class_name]["names"]:
            return True
        class_name = classes[class_name]["inherits"]
    return False


# Members every object or script has, and things that are not API references.
COMMON = {"new", "instantiate", "gd", "tscn", "tres", "x", "y", "z", "w", "r", "g", "b", "a", "size", "position", "end", "cs", "md", "json", "cfg", "import", "gdshader", "res", "png", "svg", "xml"}
pattern = re.compile(r"\b([A-Z][A-Za-z0-9]+)\.([A-Za-z_][A-Za-z0-9_]*)")
problems: dict[tuple[str, str], list[str]] = {}
checked = 0
for path in sorted(knowledge.rglob("*.md")):
    for line_number, line in enumerate(path.read_text(encoding="utf-8").split("\n"), 1):
        # Lines that show what NOT to write are expected to name things that do not exist.
        if re.search(r"WRONG|REMOVED|removed|renamed|does not exist|no longer|Godot 3|doesn't exist|was replaced|deprecated|not exist|→|->|instead of|old |no `|has no|There is no|there is no", line):
            continue
        for class_name, member in pattern.findall(line):
            if class_name not in classes or member in COMMON:
                continue
            checked += 1
            if not has_member(class_name, member):
                problems.setdefault((class_name, member), []).append(f"{path.relative_to(knowledge)}:{line_number}")
print("references checked:", checked, "distinct unknown:", len(problems))
for (class_name, member), places in sorted(problems.items()):
    print(f"{class_name}.{member}  <- {', '.join(places[:3])}")
