#pragma once

#ifdef TOOLS_ENABLED

#include "scene/gui/text_edit.h"
#include "core/templates/hash_map.h"

// TextEdit subclass used as the AI Assistant's chat input field.
// Provides inline "chip" rendering for node mentions using TextEdit's
// built-in inline object system (set_inline_object_handlers).
//
// Each mention is stored as a single Unicode Private Use Area character
// (U+E000 onwards) in the text buffer. The inline object parser detects
// these characters and the drawer renders visual chips (icon + name).
// This ensures mentions are atomic (single character = single backspace
// to delete), non-editable (can't type inside a single char), and
// visually distinct.
class AIMentionTextEdit : public TextEdit {
	GDCLASS(AIMentionTextEdit, TextEdit);

public:
	struct MentionData {
		String node_name;  // Display name: node name or filename.
		String node_path;  // Full path: NodePath string for nodes, res:// path for files.
		String node_type;  // Class name for nodes (e.g. "Node3D"); empty for files.
		bool   is_file = false; // True when this mention refers to a filesystem file.
	};

private:
	// Maps PUA character → mention data.
	// Each mention is a single Unicode Private Use Area character (U+E000+N)
	// in the text buffer. The inline object system renders a visual chip
	// before the character, and the syntax highlighter makes the character
	// itself transparent so only the chip is visible.
	HashMap<char32_t, MentionData> mentions;

	// Next available PUA codepoint.
	char32_t next_pua_char = 0xE000;

	// Inline object callbacks.
	Array _mention_parse(const String &p_text);
	void _mention_draw(const Dictionary &p_info, const Rect2 &p_rect);
	void _mention_click(const Dictionary &p_info, const Rect2 &p_rect);

protected:
	virtual bool can_drop_data(const Point2 &p_point, const Variant &p_data) const override;
	virtual void drop_data(const Point2 &p_point, const Variant &p_data) override;
	void _notification(int p_what);
	static void _bind_methods();

public:
	// Insert a scene-node mention chip at the current caret position.
	char32_t insert_mention(const String &p_node_name, const String &p_node_path, const String &p_node_type);

	// Insert a filesystem-file mention chip at the current caret position.
	// p_file_path should be a res:// path.
	char32_t insert_file_mention(const String &p_filename, const String &p_file_path);

	// Get the text with mention characters expanded to "[Node:Name path=... type=...]" for AI.
	String get_text_with_expanded_mentions() const;

	// Get the text with mentions rendered as "@NodeName" for display (BBCode).
	String get_text_with_mention_bbcode() const;

	// Clear all mention data (call when clearing the input field).
	void clear_mentions();

	// Check if a character is a PUA mention character.
	bool is_mention_char(char32_t c) const;

	// Get mention data for a PUA character. Returns true if found.
	bool get_mention_data(char32_t c, MentionData &r_data) const;

	AIMentionTextEdit();
};

#endif // TOOLS_ENABLED
