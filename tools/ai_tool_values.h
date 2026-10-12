#pragma once

#include "core/object/object.h"
#include "core/string/ustring.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

// Tool arguments arrive as JSON, which has no vectors, colours or resources.
// This turns a JSON value into what a property of a given type holds:
//
//   "Vector2(100, 50)", "Color(1, 0, 0)"   engine values written as text
//   [100, 50]                              a vector, colour or rect from its numbers
//   "#ff0000"                              a colour
//   "res://icon.svg"                       a resource, loaded
//   {"$resource": "RectangleShape2D",      a new resource of that class, with
//    "size": "Vector2(32, 32)"}            its own properties set the same way
//
// Anything that cannot be made into the type is an error with a message the
// model can act on; nothing is guessed.
class AIToolValues {
public:
	// p_type NIL: the property takes any type; the value is read as written.
	static bool to_variant(const Variant &p_json, Variant::Type p_type, Variant &r_value, String &r_error, int p_depth = 0);

	// The type of p_object's property p_name. False if it has no such property.
	static bool get_property_type(const Object *p_object, const String &p_name, Variant::Type &r_type);

	// Converts every entry of p_properties for p_object. All or nothing:
	// on false, r_error names the property and nothing has been converted.
	static bool convert_properties(const Object *p_object, const Dictionary &p_properties, Dictionary &r_values, String &r_error, int p_depth = 0);

	// Classes a tool may instantiate: part of the engine's own API, and not
	// the editor's or the assistant's.
	static bool is_instantiable(const String &p_class, const String &p_base, String &r_error);

	static const int MAX_DEPTH = 4; // Of resources inside resources.
};
