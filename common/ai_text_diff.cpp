#include "ai_text_diff.h"

#include "core/templates/vector.h"
#include "core/variant/variant.h"

namespace {

enum Change {
	KEPT,
	REMOVED,
	ADDED,
};

struct Line {
	Change change = KEPT;
	String text;
};

void push(Vector<Line> &r_lines, Change p_change, const String &p_text) {
	Line line;
	line.change = p_change;
	line.text = p_text;
	r_lines.push_back(line);
}

// The lines of p_old[p_old_from, p_old_to) and p_new[p_new_from, p_new_to),
// matched up by their longest common subsequence.
void compare(const PackedStringArray &p_old, int p_old_from, int p_old_to, const PackedStringArray &p_new, int p_new_from, int p_new_to, Vector<Line> &r_lines) {
	const int rows = p_old_to - p_old_from;
	const int columns = p_new_to - p_new_from;
	if ((int64_t)rows * columns > AITextDiff::MAX_COMPARED_CELLS) {
		for (int i = p_old_from; i < p_old_to; i++) {
			push(r_lines, REMOVED, p_old[i]);
		}
		for (int i = p_new_from; i < p_new_to; i++) {
			push(r_lines, ADDED, p_new[i]);
		}
		return;
	}

	// common[i][j]: length of the longest common subsequence of what follows
	// line i of the old text and line j of the new one.
	Vector<int> common;
	common.resize_initialized((rows + 1) * (columns + 1));
	int *cells = common.ptrw();
	const int width = columns + 1;
	for (int i = rows - 1; i >= 0; i--) {
		for (int j = columns - 1; j >= 0; j--) {
			cells[i * width + j] = p_old[p_old_from + i] == p_new[p_new_from + j]
					? cells[(i + 1) * width + j + 1] + 1
					: MAX(cells[(i + 1) * width + j], cells[i * width + j + 1]);
		}
	}

	int i = 0;
	int j = 0;
	while (i < rows && j < columns) {
		if (p_old[p_old_from + i] == p_new[p_new_from + j]) {
			push(r_lines, KEPT, p_old[p_old_from + i]);
			i++;
			j++;
		} else if (cells[(i + 1) * width + j] >= cells[i * width + j + 1]) {
			push(r_lines, REMOVED, p_old[p_old_from + i++]);
		} else {
			push(r_lines, ADDED, p_new[p_new_from + j++]);
		}
	}
	while (i < rows) {
		push(r_lines, REMOVED, p_old[p_old_from + i++]);
	}
	while (j < columns) {
		push(r_lines, ADDED, p_new[p_new_from + j++]);
	}
}

} // namespace

String AITextDiff::describe(const String &p_old, const String &p_new, int p_context_lines) {
	const PackedStringArray old_lines = p_old.replace("\r\n", "\n").split("\n");
	const PackedStringArray new_lines = p_new.replace("\r\n", "\n").split("\n");

	// What the two start and end with is the same by definition; only the
	// stretch between is compared.
	int prefix = 0;
	while (prefix < old_lines.size() && prefix < new_lines.size() && old_lines[prefix] == new_lines[prefix]) {
		prefix++;
	}
	int suffix = 0;
	while (suffix < old_lines.size() - prefix && suffix < new_lines.size() - prefix &&
			old_lines[old_lines.size() - 1 - suffix] == new_lines[new_lines.size() - 1 - suffix]) {
		suffix++;
	}
	if (prefix == old_lines.size() && prefix == new_lines.size()) {
		return String();
	}

	Vector<Line> lines;
	for (int i = 0; i < prefix; i++) {
		push(lines, KEPT, old_lines[i]);
	}
	compare(old_lines, prefix, old_lines.size() - suffix, new_lines, prefix, new_lines.size() - suffix, lines);
	for (int i = old_lines.size() - suffix; i < old_lines.size(); i++) {
		push(lines, KEPT, old_lines[i]);
	}

	// A kept line is shown when a change is within reach of it.
	Vector<bool> shown;
	shown.resize_initialized(lines.size());
	for (int i = 0; i < lines.size(); i++) {
		if (lines[i].change == KEPT) {
			continue;
		}
		for (int around = MAX(0, i - p_context_lines); around <= MIN(lines.size() - 1, i + p_context_lines); around++) {
			shown.write[around] = true;
		}
	}

	String out;
	bool skipping = false;
	for (int i = 0; i < lines.size(); i++) {
		if (!shown[i]) {
			if (!skipping) {
				out += "  ...\n";
			}
			skipping = true;
			continue;
		}
		skipping = false;
		out += String(lines[i].change == ADDED ? "+ " : (lines[i].change == REMOVED ? "- " : "  ")) + lines[i].text + "\n";
	}
	return out;
}
