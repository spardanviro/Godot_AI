#pragma once

#ifdef TOOLS_ENABLED

#include "ai_mention_text_edit.h"

#include "scene/main/node.h"

class InputEventKey;
class ItemList;
class Panel;

// The popup that completes "@partial" in the input field into a mention chip
// for a scene node or a project file. The popup is a plain Control parented to
// the editor root, so it floats above all docks without becoming an OS window
// (a PopupPanel would steal focus from the field being typed in).
class AIMentionAutocomplete : public Node {
	GDCLASS(AIMentionAutocomplete, Node);

	struct NodeEntry {
		String name;
		String path;
		String type;
	};

	struct FileEntry {
		String name; // File name only (e.g. "player.gd").
		String path; // Full res:// path.
	};

	AIMentionTextEdit *input_field = nullptr;
	Panel *popup = nullptr;
	ObjectID popup_id; // The editor root may free the popup before we do.
	ItemList *list = nullptr;

	int anchor_line = -1; // Where the "@" being completed sits.
	int anchor_column = -1;
	bool committing = false; // Inserting a chip must not re-open the popup.
	Vector<NodeEntry> node_entries;
	Vector<FileEntry> file_entries;

	void _attach_popup();
	void _on_text_changed();
	void _show(int p_line, int p_column, const String &p_partial);
	void _commit(int p_item);
	static Vector<NodeEntry> _collect_scene_nodes();
	static void _collect_files(const String &p_dir, const String &p_partial_lower, Vector<FileEntry> &r_files, int p_limit);
	// The project's mentionable files, listed once and reused while the
	// user types: walking res:// on every key press stalls large projects.
	Vector<FileEntry> file_cache;
	uint64_t file_cache_msec = 0;
	void _find_files(const String &p_partial_lower, Vector<FileEntry> &r_files);

protected:
	void _notification(int p_what);
	static void _bind_methods() {}

public:
	// The field whose "@" tokens are completed. Call once.
	void attach(AIMentionTextEdit *p_input_field);
	// Navigation and commit keys while the popup is open. Returns true if the
	// key was used and must not reach the field.
	bool handle_key(const Ref<InputEventKey> &p_key);
	void hide_popup();

	AIMentionAutocomplete();
	~AIMentionAutocomplete();
};

#endif // TOOLS_ENABLED
