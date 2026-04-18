#ifdef TOOLS_ENABLED

#include "ai_mention_text_edit.h"

#include "core/io/file_access.h"
#include "core/object/callable_mp.h"
#include "editor/editor_node.h"
#include "editor/editor_interface.h"
#include "scene/main/node.h"

void AIMentionTextEdit::_bind_methods() {}

void AIMentionTextEdit::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			// Register the inline object handlers for chip rendering.
			set_inline_object_handlers(
					callable_mp(this, &AIMentionTextEdit::_mention_parse),
					callable_mp(this, &AIMentionTextEdit::_mention_draw),
					callable_mp(this, &AIMentionTextEdit::_mention_click));
		} break;
	}
}

// --- Inline object callbacks ---

Array AIMentionTextEdit::_mention_parse(const String &p_text) {
	Array result;

	// Obtain font metrics once for accurate chip width calculation.
	Ref<Font> font = get_theme_font(SNAME("font"), SNAME("TextEdit"));
	int font_size_val = get_theme_font_size(SNAME("font_size"), SNAME("TextEdit"));
	if (font_size_val <= 0) {
		font_size_val = 14;
	}
	const float line_height = (float)font_size_val;

	for (int i = 0; i < p_text.length(); i++) {
		const char32_t c = p_text[i];
		if (c >= 0xE000 && c <= 0xF8FF && mentions.has(c)) {
			const MentionData &md = mentions[c];

			// Measure actual text pixel width using the font.
			float text_px = 0.0f;
			if (font.is_valid()) {
				text_px = font->get_string_size(md.node_name, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size_val).x;
			}

			// Chip layout: [4px pad] [icon: line_height*0.7] [3px gap] [text] [6px pad]
			const float icon_px = line_height * 0.7f;
			const float total_px = 4.0f + icon_px + 3.0f + text_px + 6.0f;

			// width_ratio is relative to line_height (font_height in TextEdit).
			const float width_ratio = total_px / line_height;

			Dictionary info;
			info["column"] = i;
			info["width_ratio"] = width_ratio;
			info["pua_char"] = (int)c;
			info["node_name"] = md.node_name;
			info["node_type"] = md.node_type;
			info["is_file"] = md.is_file;
			result.push_back(info);
		}
	}
	return result;
}

void AIMentionTextEdit::_mention_draw(const Dictionary &p_info, const Rect2 &p_rect) {
	if (!p_info.has("node_name") || !p_info.has("node_type")) {
		return;
	}

	const String node_name = p_info["node_name"];
	const String node_type = p_info["node_type"];
	const bool is_file = p_info.has("is_file") && bool(p_info["is_file"]);

	// File chips: dark green tones.  Node chips: dark navy-blue.
	const Color bg_color     = is_file ? Color(0.10f, 0.20f, 0.12f, 1.0f)
	                                   : Color(0.12f, 0.16f, 0.25f, 1.0f);
	const Color text_color   = is_file ? Color(0.45f, 0.95f, 0.55f)   // Mint green.
	                                   : Color(0.38f, 0.82f, 1.0f);    // Bright sky-blue.
	const Color border_color = is_file ? Color(0.25f, 0.60f, 0.35f, 0.6f)
	                                   : Color(0.25f, 0.45f, 0.70f, 0.6f);

	// Draw background.
	const Rect2 chip_rect = p_rect.grow(-1);
	draw_rect(chip_rect, bg_color);

	// Draw border.
	draw_rect(chip_rect, border_color, false, 1.0f);

	// Layout constants (must match _mention_parse).
	const float icon_size = chip_rect.size.y * 0.7f;
	const float pad_left = 4.0f;
	const float icon_text_gap = 3.0f;

	const float icon_x = chip_rect.position.x + pad_left;
	const float icon_y = chip_rect.position.y + (chip_rect.size.y - icon_size) * 0.5f;

	Ref<Texture2D> icon;
	if (is_file) {
		// Use generic file icon from editor theme.
		icon = get_theme_icon(SNAME("File"), SNAME("EditorIcons"));
	} else if (EditorNode::get_singleton()) {
		icon = EditorNode::get_singleton()->get_class_icon(node_type, "Node");
	}
	if (icon.is_valid()) {
		draw_texture_rect(icon, Rect2(icon_x, icon_y, icon_size, icon_size), false);
	}

	// Draw name text.
	Ref<Font> font = get_theme_font(SNAME("font"), SNAME("TextEdit"));
	int font_size_val = get_theme_font_size(SNAME("font_size"), SNAME("TextEdit"));
	if (font_size_val <= 0) {
		font_size_val = 14;
	}
	if (font.is_valid()) {
		const float text_x = icon_x + icon_size + icon_text_gap;
		const float text_y = chip_rect.position.y + chip_rect.size.y * 0.5f + font_size_val * 0.35f;
		const float max_text_w = chip_rect.size.x - pad_left - icon_size - icon_text_gap - 6.0f;
		draw_string(font, Vector2(text_x, text_y), node_name, HORIZONTAL_ALIGNMENT_LEFT, max_text_w, font_size_val, text_color);
	}
}

void AIMentionTextEdit::_mention_click(const Dictionary &p_info, const Rect2 &p_rect) {
	// For now, clicking a chip does nothing special.
	// Could open a context menu (delete, go to node) in the future.
}

// --- Mention insertion ---

char32_t AIMentionTextEdit::insert_mention(const String &p_node_name, const String &p_node_path, const String &p_node_type) {
	// Allocate a PUA character for this mention.
	char32_t pua = next_pua_char++;
	if (next_pua_char > 0xF8FF) {
		ERR_PRINT("[AI Assistant] PUA character range exhausted (6400 mentions). Wrapping around — older mentions may display incorrectly.");
		next_pua_char = 0xE000;
	}

	MentionData md;
	md.node_name = p_node_name;
	md.node_path = p_node_path;
	md.node_type = p_node_type;
	mentions[pua] = md;

	// Insert the single PUA character at the caret.
	String s;
	s += pua;
	insert_text_at_caret(s);

	return pua;
}

char32_t AIMentionTextEdit::insert_file_mention(const String &p_filename, const String &p_file_path) {
	char32_t pua = next_pua_char++;
	if (next_pua_char > 0xF8FF) {
		ERR_PRINT("[AI Assistant] PUA character range exhausted. Wrapping around.");
		next_pua_char = 0xE000;
	}

	MentionData md;
	md.node_name = p_filename;
	md.node_path = p_file_path;
	md.node_type = "";
	md.is_file   = true;
	mentions[pua] = md;

	String s;
	s += pua;
	insert_text_at_caret(s);
	return pua;
}

// --- Text extraction ---

String AIMentionTextEdit::get_text_with_expanded_mentions() const {
#ifdef TOOLS_ENABLED
	const String raw = get_text();
	Node *scene_root = EditorInterface::get_singleton()->get_edited_scene_root();

	String result;
	for (int i = 0; i < raw.length(); i++) {
		const char32_t c = raw[i];
		if (c >= 0xE000 && c <= 0xF8FF && mentions.has(c)) {
			const MentionData &md = mentions[c];

			if (md.is_file) {
				// Inline the file content so the AI has full context.
				Ref<FileAccess> fa = FileAccess::open(md.node_path, FileAccess::READ);
				if (fa.is_valid()) {
					const int MAX_CHARS = 50000;
					String content = fa->get_as_utf8_string();
					fa.unref();
					bool truncated = content.length() > MAX_CHARS;
					if (truncated) {
						content = content.substr(0, MAX_CHARS);
					}
					result += "--- FILE: " + md.node_name + " (" + md.node_path + ") ---\n";
					result += content;
					result += "\n--- END FILE ---";
					if (truncated) {
						result += "\n[truncated at " + itos(MAX_CHARS) + " chars]";
					}
				} else {
					result += "[File not found: " + md.node_path + "]";
				}
			} else {
				// Build rich context string for the AI (scene node).
				String info = "[Node:" + md.node_name +
						" path=" + md.node_path +
						" type=" + md.node_type;
				if (scene_root) {
					Node *found = scene_root->get_node_or_null(NodePath(md.node_path));
					if (found && found->get_child_count() > 0) {
						info += " children=" + itos(found->get_child_count());
					}
				}
				info += "]";
				result += info;
			}
		} else {
			result += c;
		}
	}
	return result;
#else
	return get_text();
#endif
}

String AIMentionTextEdit::get_text_with_mention_bbcode() const {
	const String raw = get_text();
	String result;
	for (int i = 0; i < raw.length(); i++) {
		const char32_t c = raw[i];
		if (c >= 0xE000 && c <= 0xF8FF && mentions.has(c)) {
			const MentionData &md = mentions[c];
			if (md.is_file) {
				// File chip: dark green pill.
				result += "[bgcolor=#0F2018][color=#5DEA80][b] \U0001F4C4 ";
				result += md.node_name;
				result += " [/b][/color][/bgcolor]";
			} else {
				// Node chip: navy-blue pill.  Use "@" as node marker — always renders.
				result += "[bgcolor=#102030][color=#5DC4F0][b] @ ";
				result += md.node_name;
				result += " [/b][/color][/bgcolor]";
			}
		} else {
			// Escape BBCode-special '[' so literal brackets don't trigger tags.
			if (c == '[') {
				result += "[lb]";
			} else {
				result += c;
			}
		}
	}
	return result;
}

void AIMentionTextEdit::clear_mentions() {
	mentions.clear();
	next_pua_char = 0xE000;
}

bool AIMentionTextEdit::is_mention_char(char32_t c) const {
	return c >= 0xE000 && c <= 0xF8FF && mentions.has(c);
}

bool AIMentionTextEdit::get_mention_data(char32_t c, MentionData &r_data) const {
	if (mentions.has(c)) {
		r_data = mentions[c];
		return true;
	}
	return false;
}

// --- Drag and drop ---

bool AIMentionTextEdit::can_drop_data(const Point2 &p_point, const Variant &p_data) const {
	if (p_data.get_type() != Variant::DICTIONARY) {
		return false;
	}
	const Dictionary d = p_data;
	if (!d.has("type")) {
		return false;
	}
	const String dtype = d["type"];
	// Accept node drags from the scene tree dock, or file drags from the filesystem dock.
	return dtype == "nodes" || dtype == "files";
}

void AIMentionTextEdit::drop_data(const Point2 &p_point, const Variant &p_data) {
	if (!can_drop_data(p_point, p_data)) {
		return;
	}
	const Dictionary d = p_data;
	const String dtype = d["type"];

	// Move the caret to the drop position so the mention is inserted there.
	const Point2i tp = get_line_column_at_pos(Point2i(p_point));
	set_caret_line(tp.y);
	set_caret_column(tp.x);
	grab_focus();

	if (dtype == "nodes") {
		if (!d.has("nodes")) {
			return;
		}
		const Array node_paths = d["nodes"];
		if (node_paths.is_empty()) {
			return;
		}

#ifdef TOOLS_ENABLED
		Node *scene_root = EditorInterface::get_singleton()->get_edited_scene_root();
		for (int i = 0; i < node_paths.size(); i++) {
			const NodePath np = node_paths[i];
			if (scene_root) {
				Node *found = scene_root->get_node_or_null(np);
				if (found) {
					insert_mention(
							String(found->get_name()),
							String(found->get_path()),
							found->get_class());
					continue;
				}
			}
			// Fallback: extract leaf name from path.
			String name;
			if (np.get_name_count() > 0) {
				name = np.get_name(np.get_name_count() - 1);
			} else {
				name = String(np).get_file();
			}
			if (!name.is_empty()) {
				insert_mention(name, String(np), "Node");
			}
		}
#endif

	} else if (dtype == "files") {
		// Files (and folders) dragged from the FileSystem dock.
		// Directory paths typically end with '/' so get_file() returns "".
		// Fall back to stripping the trailing slash first.
		if (!d.has("files")) {
			return;
		}
		const Array file_paths = d["files"];
		for (int i = 0; i < file_paths.size(); i++) {
			const String path = file_paths[i];
			if (path.is_empty()) {
				continue;
			}
			String fname = path.get_file();
			if (fname.is_empty()) {
				// Directory path — strip trailing slash and grab last component.
				fname = path.rstrip("/").get_file();
			}
			if (!fname.is_empty()) {
				insert_file_mention(fname, path);
			}
		}
	}
}

AIMentionTextEdit::AIMentionTextEdit() {}

#endif // TOOLS_ENABLED
