#include "ai_tool_values.h"

#include "core/io/resource.h"
#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/string/char_utils.h"
#include "core/variant/variant_parser.h"
#include "core/variant/variant_utility.h"

namespace {

const char *RESOURCE_KEY = "$resource";

bool all_numbers(const Array &p_array) {
	for (int i = 0; i < p_array.size(); i++) {
		if (!p_array[i].is_num()) {
			return false;
		}
	}
	return true;
}

// A vector, colour or rect from the list of its components.
bool from_components(const Array &p_numbers, Variant::Type p_type, Variant &r_value) {
	const int count = p_numbers.size();
	const auto at = [&](int p_index) { return double(p_numbers[p_index]); };
	switch (p_type) {
		case Variant::VECTOR2:
			if (count == 2) {
				r_value = Vector2(at(0), at(1));
				return true;
			}
			break;
		case Variant::VECTOR2I:
			if (count == 2) {
				r_value = Vector2i(at(0), at(1));
				return true;
			}
			break;
		case Variant::VECTOR3:
			if (count == 3) {
				r_value = Vector3(at(0), at(1), at(2));
				return true;
			}
			break;
		case Variant::VECTOR3I:
			if (count == 3) {
				r_value = Vector3i(at(0), at(1), at(2));
				return true;
			}
			break;
		case Variant::VECTOR4:
			if (count == 4) {
				r_value = Vector4(at(0), at(1), at(2), at(3));
				return true;
			}
			break;
		case Variant::RECT2:
			if (count == 4) {
				r_value = Rect2(at(0), at(1), at(2), at(3));
				return true;
			}
			break;
		case Variant::COLOR:
			if (count == 3 || count == 4) {
				r_value = Color(at(0), at(1), at(2), count == 4 ? at(3) : 1.0);
				return true;
			}
			break;
		default:
			break;
	}
	return false;
}

// "Vector2(1, 2)", "true", "3.5", "[1, 2]" — an engine value written as text.
// Values only: the parser also knows how to build objects and load resources
// from text ("Object(GDScript, ...)"), which would let a property value carry
// a script past the permission that covers scripts. Text that asks for one
// is not parsed at all, so nothing is built and thrown away.
bool parse_engine_text(const String &p_text, Variant &r_value) {
	static const char *constructors[] = { "Object", "Resource", "SubResource", "ExtResource", nullptr };
	const String squeezed = p_text.remove_chars(" \t\r\n");
	for (int i = 0; constructors[i]; i++) {
		if (squeezed.contains(String(constructors[i]) + "(")) {
			return false;
		}
	}
	VariantParser::StreamString stream;
	stream.s = p_text;
	String error;
	int error_line = 0;
	Variant parsed;
	if (VariantParser::parse(&stream, parsed, error, error_line) != OK || parsed.get_type() == Variant::OBJECT) {
		return false;
	}
	r_value = parsed;
	return true;
}

bool fits(const Variant &p_value, Variant::Type p_type) {
	if (p_type == Variant::NIL || p_value.get_type() == p_type) {
		return true;
	}
	// An object property can be cleared.
	return p_type == Variant::OBJECT && p_value.get_type() == Variant::NIL;
}

} // namespace

bool AIToolValues::is_instantiable(const String &p_class, const String &p_base, String &r_error) {
	if (!ClassDB::class_exists(p_class)) {
		r_error = "there is no class named '" + p_class + "'.";
		return false;
	}
	if (!ClassDB::is_parent_class(p_class, p_base)) {
		r_error = "'" + p_class + "' is not a " + p_base + ".";
		return false;
	}
	// The assistant's own classes ("AI" + capital) and the editor's are not for projects.
	const bool own_class = p_class.length() > 2 && p_class.begins_with("AI") && is_ascii_upper_case(p_class[2]);
	if (own_class || ClassDB::get_api_type(p_class) != ClassDB::API_CORE) {
		r_error = "'" + p_class + "' is an editor class and cannot be created in a project.";
		return false;
	}
	// Code comes from the project's files, where it is written with the tool
	// and under the permission for that — never out of a property value.
	if (ClassDB::is_parent_class(p_class, "Script") || ClassDB::is_parent_class(p_class, "PackedScene") || ClassDB::is_parent_class(p_class, "GDExtension")) {
		r_error = "a " + p_class + " is not created in place. Write the file with write_file and refer to it by its res:// path.";
		return false;
	}
	if (!ClassDB::can_instantiate(p_class)) {
		r_error = "'" + p_class + "' is abstract and cannot be created; use one of its subclasses.";
		return false;
	}
	return true;
}

bool AIToolValues::get_property_type(const Object *p_object, const String &p_name, Variant::Type &r_type) {
	List<PropertyInfo> properties;
	p_object->get_property_list(&properties);
	for (const PropertyInfo &info : properties) {
		if (info.name == p_name) {
			// A property declared as Variant takes anything.
			r_type = (info.usage & PROPERTY_USAGE_NIL_IS_VARIANT) ? Variant::NIL : info.type;
			return true;
		}
	}
	bool valid = false;
	const Variant current = p_object->get(p_name, &valid);
	if (valid) {
		r_type = current.get_type();
	}
	return valid;
}

bool AIToolValues::convert_properties(const Object *p_object, const Dictionary &p_properties, Dictionary &r_values, String &r_error, int p_depth) {
	Dictionary converted;
	for (const Variant *key = p_properties.next(nullptr); key; key = p_properties.next(key)) {
		const String name = *key;
		if (name == RESOURCE_KEY) {
			continue;
		}
		// One way in for scripts: attach_script, which has the permission for it.
		if (name == "script") {
			r_error = "'script' is not set as a property. Attach a script file with attach_script.";
			return false;
		}
		Variant::Type type = Variant::NIL;
		if (!get_property_type(p_object, name, type)) {
			r_error = p_object->get_class() + " has no property '" + name + "'.";
			return false;
		}
		Variant value;
		String error;
		if (!to_variant(p_properties[*key], type, value, error, p_depth)) {
			r_error = "'" + name + "': " + error;
			return false;
		}
		converted[name] = value;
	}
	r_values = converted;
	return true;
}

bool AIToolValues::to_variant(const Variant &p_json, Variant::Type p_type, Variant &r_value, String &r_error, int p_depth) {
	const String type_name = Variant::get_type_name(p_type);

	switch (p_json.get_type()) {
		case Variant::NIL: {
			r_value = Variant();
			return true;
		}

		case Variant::DICTIONARY: {
			const Dictionary object = p_json;
			if (!object.has(RESOURCE_KEY)) {
				if (p_type == Variant::DICTIONARY || p_type == Variant::NIL) {
					r_value = object;
					return true;
				}
				r_error = "expects " + type_name + ", not an object. To create a resource write {\"$resource\": \"ClassName\", ...}.";
				return false;
			}
			if (p_type != Variant::OBJECT && p_type != Variant::NIL) {
				r_error = "expects " + type_name + ", not a resource.";
				return false;
			}
			if (p_depth >= MAX_DEPTH) {
				r_error = "resources are nested too deeply.";
				return false;
			}
			const String class_name = object[RESOURCE_KEY];
			if (!is_instantiable(class_name, "Resource", r_error)) {
				return false;
			}
			Ref<Resource> resource = Object::cast_to<Resource>(ClassDB::instantiate(class_name));
			if (resource.is_null()) {
				r_error = "'" + class_name + "' could not be created.";
				return false;
			}
			Dictionary values;
			if (!convert_properties(resource.ptr(), object, values, r_error, p_depth + 1)) {
				return false;
			}
			for (const Variant *key = values.next(nullptr); key; key = values.next(key)) {
				resource->set(*key, values[*key]);
			}
			r_value = resource;
			return true;
		}

		case Variant::STRING: {
			const String text = p_json;
			if (p_type == Variant::STRING || p_type == Variant::STRING_NAME || p_type == Variant::NODE_PATH) {
				r_value = VariantUtilityFunctions::type_convert(text, p_type);
				return true;
			}
			if (p_type == Variant::OBJECT) {
				if (text.is_empty()) {
					r_value = Variant();
					return true;
				}
				if (!text.begins_with("res://") || !ResourceLoader::exists(text)) {
					r_error = "expects a resource: a res:// path that exists, or {\"$resource\": \"ClassName\", ...}. '" + text + "' is neither.";
					return false;
				}
				const Ref<Resource> resource = ResourceLoader::load(text);
				if (resource.is_null()) {
					r_error = "'" + text + "' could not be loaded.";
					return false;
				}
				r_value = resource;
				return true;
			}
			if (p_type == Variant::COLOR && Color::html_is_valid(text)) {
				r_value = Color::html(text);
				return true;
			}
			Variant parsed;
			if (parse_engine_text(text, parsed)) {
				if (fits(parsed, p_type)) {
					r_value = parsed;
					return true;
				}
				if (Variant::can_convert_strict(parsed.get_type(), p_type)) {
					r_value = VariantUtilityFunctions::type_convert(parsed, p_type);
					return true;
				}
			}
			if (p_type == Variant::NIL) {
				r_value = text; // Any type goes, and it did not read as anything else.
				return true;
			}
			r_error = "expects " + type_name + ", and '" + text + "' is not one. Write it the way GDScript does, e.g. Vector2(100, 50).";
			return false;
		}

		case Variant::ARRAY: {
			const Array array = p_json;
			if (all_numbers(array) && from_components(array, p_type, r_value)) {
				return true;
			}
			if (p_type == Variant::ARRAY || p_type == Variant::NIL) {
				r_value = array;
				return true;
			}
			if (Variant::can_convert_strict(Variant::ARRAY, p_type)) {
				r_value = VariantUtilityFunctions::type_convert(array, p_type);
				return true;
			}
			r_error = "expects " + type_name + "; a list of " + itos(array.size()) + " values is not one.";
			return false;
		}

		case Variant::BOOL:
		case Variant::INT:
		case Variant::FLOAT: {
			if (p_type == Variant::NIL) {
				// JSON has one number type; a whole number is meant as an integer.
				const bool whole = p_json.get_type() == Variant::FLOAT && double(p_json) == Math::floor(double(p_json)) && Math::abs(double(p_json)) < 1e15;
				r_value = whole ? Variant(int64_t(double(p_json))) : p_json;
				return true;
			}
			if (p_type == Variant::BOOL || p_type == Variant::INT || p_type == Variant::FLOAT) {
				r_value = VariantUtilityFunctions::type_convert(p_json, p_type);
				return true;
			}
			r_error = "expects " + type_name + ", not a single number.";
			return false;
		}

		default:
			break;
	}
	r_error = "expects " + type_name + ".";
	return false;
}
