"""Embeds the files under knowledge/ into the editor binary as the built-in defaults."""

import os

import methods


def knowledge_builder(target, source, env):
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "knowledge")

    blob = bytearray()
    entries = []
    for src in sorted(str(s) for s in source):
        rel_path = os.path.relpath(os.path.abspath(src), root).replace("\\", "/")
        # Checkouts on Windows may carry CRLF; the runtime format is LF only.
        data = methods.get_buffer(src).replace(b"\r\n", b"\n")
        entries.append((rel_path, len(blob), len(data)))
        blob += data

    compressed = methods.compress_buffer(bytes(blob))

    with methods.generated_wrapper(str(target[0])) as file:
        file.write(f"""\
struct AIKnowledgeBuiltinFile {{
	const char *path;
	int offset;
	int size;
}};

inline constexpr int _ai_knowledge_uncompressed_size = {len(blob)};
inline constexpr int _ai_knowledge_compressed_size = {len(compressed)};
inline constexpr unsigned char _ai_knowledge_compressed[] = {{
	{methods.format_buffer(compressed, 1)}
}};

inline constexpr AIKnowledgeBuiltinFile _ai_knowledge_files[] = {{
""")
        for rel_path, offset, size in entries:
            file.write(f'\t{{ "{rel_path}", {offset}, {size} }},\n')
        file.write("\t{ nullptr, 0, 0 },\n};\n")


def model_specs_builder(target, source, env):
    """Embeds data/model_specs.json, the model facts the editor starts from."""
    data = methods.get_buffer(str(source[0])).replace(b"\r\n", b"\n")
    compressed = methods.compress_buffer(data)

    with methods.generated_wrapper(str(target[0])) as file:
        file.write(f"""\
inline constexpr int _ai_model_specs_uncompressed_size = {len(data)};
inline constexpr int _ai_model_specs_compressed_size = {len(compressed)};
inline constexpr unsigned char _ai_model_specs_compressed[] = {{
	{methods.format_buffer(compressed, 1)}
}};
""")
