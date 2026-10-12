#ifdef TOOLS_ENABLED

#include "ai_mention_autocomplete.h"

#include "core/os/os.h"

#include "core/input/input_event.h"
#include "core/io/dir_access.h"
#include "core/object/callable_mp.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "scene/gui/item_list.h"
#include "scene/gui/panel.h"

static const int MAX_FILE_RESULTS = 30;
static const int FILE_CACHE_LIMIT = 5000;
static const uint64_t FILE_CACHE_LIFETIME_MSEC = 5000;
static const float POPUP_WIDTH = 250.0f;
static const float POPUP_MAX_HEIGHT = 200.0f;
static const float ITEM_HEIGHT = 26.0f;
static const float POPUP_PADDING = 8.0f;
static const Vector2 CARET_OFFSET(-4.0f, -4.0f);

// Letters, digits, underscore, hyphen and dot, so file names such as
// "player.gd" or "my-script" can be typed after "@".
static bool _is_mention_word_char(char32_t p_char) {
	return (p_char >= 'a' && p_char <= 'z') || (p_char >= 'A' && p_char <= 'Z') ||
			(p_char >= '0' && p_char <= '9') || p_char == '_' || p_char == '-' || p_char == '.';
}

void AIMentionAutocomplete::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			// Deferred: the editor root may still be setting up its children.
			// Bound to this object, so the call is dropped if we are freed first
			// (a deferred add_child on the root would then receive a dead popup).
			callable_mp(this, &AIMentionAutocomplete::_attach_popup).call_deferred();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			// The dock leaves the tree whenever it is moved or floated, not only
			// on shutdown, so the popup is hidden here and freed in the destructor.
			hide_popup();
		} break;
	}
}

void AIMentionAutocomplete::_attach_popup() {
	Control *base = EditorInterface::get_singleton() ? EditorInterface::get_singleton()->get_base_control() : nullptr;
	if (base && popup && !popup->get_parent()) {
		base->add_child(popup);
	}
}

void AIMentionAutocomplete::attach(AIMentionTextEdit *p_input_field) {
	input_field = p_input_field;
	input_field->connect("text_changed", callable_mp(this, &AIMentionAutocomplete::_on_text_changed));
}

void AIMentionAutocomplete::hide_popup() {
	if (popup && popup->is_visible()) {
		popup->hide();
	}
	anchor_line = -1;
	anchor_column = -1;
}

bool AIMentionAutocomplete::handle_key(const Ref<InputEventKey> &p_key) {
	if (!popup || !popup->is_visible()) {
		return false;
	}
	// The caret was moved away from the "@" token (arrow keys do not change
	// the text, so nothing closed the popup): it no longer applies.
	if (input_field->get_caret_line() != anchor_line || input_field->get_caret_column() <= anchor_column) {
		hide_popup();
		return false;
	}
	const int item_count = list->get_item_count();
	const PackedInt32Array selected = list->get_selected_items();
	const int current = selected.is_empty() ? 0 : selected[0];

	switch (p_key->get_keycode()) {
		case Key::ESCAPE:
			hide_popup();
			return true;
		case Key::UP:
		case Key::DOWN:
			if (item_count > 0) {
				const int step = p_key->get_keycode() == Key::UP ? -1 : 1;
				list->select((current + step + item_count) % item_count);
				list->ensure_current_is_visible();
			}
			return true;
		case Key::ENTER:
		case Key::KP_ENTER:
		case Key::TAB:
			if (!selected.is_empty()) {
				_commit(selected[0]);
			}
			return true;
		default:
			return false; // Typed characters go on filtering the list.
	}
}

// Shows the popup while the caret sits right after an "@partial" token.
void AIMentionAutocomplete::_on_text_changed() {
	if (committing) {
		return;
	}
	const int caret_column = input_field->get_caret_column();
	const int caret_line = input_field->get_caret_line();
	const String line = input_field->get_line(caret_line);

	int at_position = -1;
	for (int i = caret_column - 1; i >= 0; i--) {
		const char32_t c = line[i];
		if (c == '@') {
			// Only at the start of a word.
			if (i == 0 || !_is_mention_word_char(line[i - 1])) {
				at_position = i;
			}
			break;
		}
		// Mention chips are single placeholder characters, not text.
		if (input_field->is_mention_char(c) || !_is_mention_word_char(c)) {
			break;
		}
	}

	if (at_position >= 0) {
		_show(caret_line, at_position, line.substr(at_position + 1, caret_column - at_position - 1));
	} else {
		hide_popup();
	}
}

void AIMentionAutocomplete::_show(int p_line, int p_column, const String &p_partial) {
	anchor_line = p_line;
	anchor_column = p_column;
	const String partial = p_partial.to_lower();

	node_entries = _collect_scene_nodes();
	file_entries.clear();
	// Files only once something is typed, or a bare "@" floods the list.
	if (!partial.is_empty()) {
		_find_files(partial, file_entries);
	}

	list->clear();
	// Item metadata: >= 0 is a node index, < 0 is -(file index + 1).
	for (int i = 0; i < node_entries.size(); i++) {
		if (!partial.is_empty() && !node_entries[i].name.to_lower().contains(partial)) {
			continue;
		}
		Ref<Texture2D> icon;
		if (EditorNode::get_singleton()) {
			icon = EditorNode::get_singleton()->get_class_icon(node_entries[i].type, "Node");
		}
		list->add_item(node_entries[i].name, icon);
		list->set_item_metadata(list->get_item_count() - 1, i);
	}
	const Ref<Texture2D> file_icon = input_field->get_theme_icon(SNAME("File"), SNAME("EditorIcons"));
	for (int i = 0; i < file_entries.size(); i++) {
		list->add_item(file_entries[i].name, file_icon);
		list->set_item_tooltip(list->get_item_count() - 1, file_entries[i].path);
		list->set_item_metadata(list->get_item_count() - 1, -(i + 1));
	}

	if (list->get_item_count() == 0) {
		hide_popup();
		return;
	}

	// Selecting programmatically must not fire item_selected (which commits).
	list->set_block_signals(true);
	list->select(0);
	list->set_block_signals(false);

	// Above the caret, in the editor root's local space.
	const float height = MIN((float)list->get_item_count() * ITEM_HEIGHT + POPUP_PADDING, POPUP_MAX_HEIGHT);
	const Vector2 caret = input_field->get_screen_position() + input_field->get_caret_draw_pos();
	Control *base = EditorInterface::get_singleton()->get_base_control();
	const Vector2 origin = base ? base->get_screen_position() : Vector2();
	popup->set_position(caret - origin + CARET_OFFSET - Vector2(0.0f, height));
	popup->set_size(Vector2(POPUP_WIDTH, height));
	popup->show();
	popup->move_to_front();
}

// Replaces the "@partial" text with a mention chip.
void AIMentionAutocomplete::_commit(int p_item) {
	if (p_item < 0 || p_item >= list->get_item_count() || anchor_column < 0) {
		return;
	}
	const int metadata = list->get_item_metadata(p_item);
	const int file_index = -(metadata + 1);
	const bool valid = metadata >= 0 ? metadata < node_entries.size() : file_index < file_entries.size();
	if (!valid) {
		return;
	}

	committing = true;
	input_field->select(anchor_line, anchor_column, anchor_line, input_field->get_caret_column());
	input_field->delete_selection();
	if (metadata >= 0) {
		const NodeEntry &node = node_entries[metadata];
		input_field->insert_mention(node.name, node.path, node.type);
	} else {
		const FileEntry &file = file_entries[file_index];
		input_field->insert_file_mention(file.name, file.path);
	}
	committing = false;

	hide_popup();
	input_field->grab_focus();
}

// Every node of the edited scene, one entry per distinct name, sorted by name.
Vector<AIMentionAutocomplete::NodeEntry> AIMentionAutocomplete::_collect_scene_nodes() {
	struct NameSorter {
		bool operator()(const NodeEntry &p_a, const NodeEntry &p_b) const {
			return p_a.name.naturalcasecmp_to(p_b.name) < 0;
		}
	};

	Vector<NodeEntry> entries;
	Node *root = EditorInterface::get_singleton()->get_edited_scene_root();
	if (!root) {
		return entries;
	}
	HashSet<String> seen;
	List<Node *> queue;
	queue.push_back(root);
	while (!queue.is_empty()) {
		Node *node = queue.front()->get();
		queue.pop_front();
		const String name = node->get_name();
		if (!seen.has(name)) {
			seen.insert(name);
			NodeEntry entry;
			entry.name = name;
			entry.path = String(node->get_path());
			entry.type = node->get_class();
			entries.push_back(entry);
		}
		for (int i = 0; i < node->get_child_count(); i++) {
			queue.push_back(node->get_child(i));
		}
	}
	entries.sort_custom<NameSorter>();
	return entries;
}

// Walks the project for text-like files whose name contains p_partial_lower.
void AIMentionAutocomplete::_find_files(const String &p_partial_lower, Vector<FileEntry> &r_files) {
	const uint64_t now = OS::get_singleton()->get_ticks_msec();
	if (file_cache_msec == 0 || now - file_cache_msec > FILE_CACHE_LIFETIME_MSEC) {
		file_cache.clear();
		_collect_files("res://", String(), file_cache, FILE_CACHE_LIMIT);
		file_cache_msec = MAX(now, (uint64_t)1);
	}
	for (int i = 0; i < file_cache.size() && r_files.size() < MAX_FILE_RESULTS; i++) {
		if (file_cache[i].name.to_lower().contains(p_partial_lower)) {
			r_files.push_back(file_cache[i]);
		}
	}
}

void AIMentionAutocomplete::_collect_files(const String &p_dir, const String &p_partial_lower, Vector<FileEntry> &r_files, int p_limit) {
	static const char *allowed_extensions[] = {
		"gd", "cs", "md", "txt", "json", "yaml", "yml", "cfg", "ini", "tscn", "tres", "res", "toml", nullptr
	};
	Ref<DirAccess> dir = DirAccess::open(p_dir);
	if (dir.is_null() || r_files.size() >= p_limit) {
		return;
	}

	Vector<String> subdirs;
	dir->list_dir_begin();
	for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
		if (entry.begins_with(".")) {
			continue; // Hidden entries, including the .godot import cache.
		}
		if (dir->current_is_dir()) {
			subdirs.push_back(entry);
			continue;
		}
		if (r_files.size() >= p_limit || !entry.to_lower().contains(p_partial_lower)) {
			continue;
		}
		const String extension = entry.get_extension().to_lower();
		for (int e = 0; allowed_extensions[e]; e++) {
			if (extension == allowed_extensions[e]) {
				FileEntry file;
				file.name = entry;
				file.path = p_dir.path_join(entry);
				r_files.push_back(file);
				break;
			}
		}
	}
	dir->list_dir_end();

	for (int i = 0; i < subdirs.size(); i++) {
		_collect_files(p_dir.path_join(subdirs[i]), p_partial_lower, r_files, p_limit);
	}
}

AIMentionAutocomplete::AIMentionAutocomplete() {
	popup = memnew(Panel);
	popup_id = popup->get_instance_id();
	popup->set_mouse_filter(Control::MOUSE_FILTER_STOP);
	popup->hide();

	list = memnew(ItemList);
	list->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	list->set_focus_mode(Control::FOCUS_NONE);
	list->connect("item_selected", callable_mp(this, &AIMentionAutocomplete::_commit));
	popup->add_child(list);
}

AIMentionAutocomplete::~AIMentionAutocomplete() {
	// The popup is parented to the editor root. If that root is already gone
	// it took the popup with it, so look it up by id.
	if (Node *node = Object::cast_to<Node>(ObjectDB::get_instance(popup_id))) {
		if (!node->get_parent()) {
			memdelete(node); // Never attached — still ours.
		} else if (node->is_inside_tree()) {
			node->queue_free();
		}
		// Otherwise its parent is being torn down and will free it.
	}
}

#endif // TOOLS_ENABLED
