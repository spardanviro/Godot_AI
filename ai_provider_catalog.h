#pragma once

#include "core/string/ustring.h"
#include "core/variant/variant.h"

// What the settings dialog can show for a provider without asking its API:
// the name to list it under and the models to offer until (or unless) the
// live model list arrives. Entries are in the order of
// AIProvider::get_provider_ids().
class AIProviderCatalog {
public:
	static int get_count();
	// Empty for an index out of range.
	static String get_id(int p_index);
	// Index of a provider id; 0 (the default provider) for an unknown one.
	static int find(const String &p_id);
	static String get_display_name(int p_index);
	// Current models, the provider's default first.
	static PackedStringArray get_known_models(int p_index);
};
