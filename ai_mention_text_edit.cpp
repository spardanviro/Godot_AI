#ifdef TOOLS_ENABLED

#include "ai_mention_text_edit.h"

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

	// Chip background: dark navy-blue rounded rect.
	const Color bg_color(0.12f, 0.16f, 0.25f, 1.0f);
	const Color text_color(0.38f, 0.82f, 1.0f); // Bright sky-blue.
	const Color border_color(0.25f, 0.45f, 0.7f, 0.6f);

	// Draw background.
	const Rect2 chip_rect = p_rect.grow(-1);
	draw_rect(chip_rect, bg_color);

	// Draw border.
	draw_rect(chip_rect, border_color, false, 1.0f);

	// Layout constants (must match _mention_parse).
	const float icon_size = chip_rect.size.y * 0.7f;
	const float pad_left = 4.0f;
	const float icon_text_gap = 3.0f;

	// Draw node type icon (left side of chip).
	const float icon_x = chip_rect.position.x + pad_left;
	const float icon_y = chip_rect.position.y + (chip_rect.size.y - icon_size) * 0.5f;

	Ref<Texture2D> icon;
	if (EditorNode::get_singleton()) {
		icon = EditorNode::get_singleton()->get_class_icon(node_type, "Node");
	}
	if (icon.is_valid()) {
		draw_texture_rect(icon, Rect2(icon_x, icon_y, icon_size, icon_size), false);
	}

	// Draw node name text.
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

			// Build rich context string for the AI.
			String info = "[Node:" + md.node_name +
					" path=" + md.node_path +
					" type=" + md.node_type;

			// Try to find the node by stored path and add children count.
			if (scene_root) {
				Node *found = scene_root->get_node_or_null(NodePath(md.node_path));
				if (found && found->get_child_count() > 0) {
					info += " children=" + itos(found->get_child_count());
				}
			}
			info += "]";
			result += info;
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
			// Render as a navy-blue pill with bright text for the chat display.
			result += "[bgcolor=#102030][color=#5DC4F0][b] \u25A3 ";
			result += md.node_name;
			result += " [/b][/color][/bgcolor]";
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
	// Accept node drags from the scene tree dock.
	return d.has("type") && String(d["type"]) == "nodes";
}

void AIMentionTextEdit::drop_data(const Point2 &p_point, const Variant &p_data) {
	if (!can_drop_data(p_point, p_data)) {
		return;
	}
	const Dictionary d = p_data;
	if (!d.has("nodes")) {
		return;
	}
	const Array node_paths = d["nodes"];
	if (node_paths.is_empty()) {
		return;
	}

	// Move the caret to the drop position so the mention is inserted there.
	const Point2i tp = get_line_column_at_pos(Point2i(p_point));
	set_caret_line(tp.y);
	set_caret_column(tp.x);
	grab_focus();

	// Insert chip mentions for each dropped node.
#ifdef TOOLS_ENABLED
	Node *scene_root = EditorInterface::get_singleton()->get_edited_scene_root();
	for (int i = 0; i < node_paths.size(); i++) {
		const NodePath np = node_paths[i];
		if (scene_root) {
			// Use the full NodePath from the drag payload for an exact lookup.
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
}

AIMentionTextEdit::AIMentionTextEdit() {}

#endif // TOOLS_ENABLED
