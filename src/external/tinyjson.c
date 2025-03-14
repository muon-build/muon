/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-FileCopyrightText: Seedo Paul <seedoeldhopaul@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "external/tinyjson.h"
#include "lang/object.h"
#include "lang/string.h"
#include "log.h"

/*******************************************************************************
 * begin tiny-json.h/c
 ******************************************************************************/

/*
<https://github.com/rafagafe/tiny-json>

  Licensed under the MIT License <http://opensource.org/licenses/MIT>.
  SPDX-License-Identifier: MIT
  Copyright (c) 2016-2018 Rafa Garcia <rafagarcia77@gmail.com>.

  Permission is hereby  granted, free of charge, to any  person obtaining a copy
  of this software and associated  documentation files (the "Software"), to deal
  in the Software  without restriction, including without  limitation the rights
  to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
  copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
  IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
  FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
  AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
  LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

*/

#define json_containerOf(ptr, type, member) ((type *)((char *)ptr - offsetof(type, member)))

/** Enumeration of codes of supported JSON properties types. */
typedef enum { JSON_OBJ, JSON_ARRAY, JSON_TEXT, JSON_BOOLEAN, JSON_INTEGER, JSON_REAL, JSON_NULL } jsonType_t;

/** Structure to handle JSON properties. */
typedef struct json_s {
	struct json_s *sibling;
	char const *name;
	union {
		char const *value;
		struct {
			struct json_s *child;
			struct json_s *last_child;
		} c;
	} u;
	jsonType_t type;
} json_t;

/** Structure to handle a heap of JSON properties. */
typedef struct jsonPool_s jsonPool_t;
struct jsonPool_s {
	json_t *(*init)(jsonPool_t *pool);
	json_t *(*alloc)(jsonPool_t *pool);
};

/** Structure to handle a heap of JSON properties. */
typedef struct jsonStaticPool_s {
	json_t *mem; /**< Pointer to array of json properties.      */
	unsigned int qty; /**< Length of the array of json properties.   */
	unsigned int nextFree; /**< The index of the next free json property. */
	jsonPool_t pool;
} jsonStaticPool_t;

/** Set of characters that defines a blank. */
static char const *const json_blank = " \n\r\t\f";

/** Set of characters that defines the end of an array or a JSON object. */
static char const *const json_endofblock = "}]";


/** Get the name of a json property.
  * @param json A valid handler of a json property.
  * @retval Pointer to null-terminated if property has name.
  * @retval Null pointer if the property is unnamed. */
static char const *
json_getName(json_t const *json)
{
	return json->name;
}

/** Get the value of a json property.
  * The type of property cannot be JSON_OBJ or JSON_ARRAY.
  * @param property A valid handler of a json property.
  * @return Pointer to null-terminated string with the value. */
static char const *
json_getValue(json_t const *property)
{
	return property->u.value;
}

/** Get the type of a json property.
  * @param json A valid handler of a json property.
  * @return The code of type.*/
static jsonType_t
json_getType(json_t const *json)
{
	return json->type;
}

/** Get the next sibling of a JSON property that is within a JSON object or array.
  * @param json A valid handler of a json property.
  * @retval The handler of the next sibling if found.
  * @retval Null pointer if the json property is the last one. */
static json_t const *
json_getSibling(json_t const *json)
{
	return json->sibling;
}

/** Get the first property of a JSON object or array.
  * @param json A valid handler of a json property.
  *             Its type must be JSON_OBJ or JSON_ARRAY.
  * @retval The handler of the first property if there is.
  * @retval Null pointer if the json object has not properties. */
static json_t const *
json_getChild(json_t const *json)
{
	return json->u.c.child;
}

/** Get the value of a json boolean property.
  * @param property A valid handler of a json object. Its type must be JSON_BOOLEAN.
  * @return The value stdbool. */
static bool
json_getBoolean(json_t const *property)
{
	return *property->u.value == 't';
}

/** Get the value of a json integer property.
  * @param property A valid handler of a json object. Its type must be JSON_INTEGER.
  * @return The value stdint. */
static int64_t
json_getInteger(json_t const *property)
{
	return strtoll(property->u.value, (char **)NULL, 10);
}

/* Search a property by its name in a JSON object. */
json_t const *
json_getProperty(json_t const *obj, char const *property)
{
	json_t const *sibling;
	for (sibling = obj->u.c.child; sibling; sibling = sibling->sibling)
		if (sibling->name && !strcmp(sibling->name, property))
			return sibling;
	return 0;
}

/* Search a property by its name in a JSON object and return its value. */
char const *
json_getPropertyValue(json_t const *obj, char const *property)
{
	json_t const *field = json_getProperty(obj, property);
	if (!field)
		return 0;
	jsonType_t type = json_getType(field);
	if (JSON_ARRAY >= type)
		return 0;
	return json_getValue(field);
}

/** Initialize a json pool.
  * @param pool The handler of the pool.
  * @return a instance of a json. */
static json_t *
json_poolInit(jsonPool_t *pool)
{
	jsonStaticPool_t *spool = json_containerOf(pool, jsonStaticPool_t, pool);
	spool->nextFree = 1;
	return spool->mem;
}

/** Create an instance of a json from a pool.
  * @param pool The handler of the pool.
  * @retval The handler of the new instance if success.
  * @retval Null pointer if the pool was empty. */
static json_t *
json_poolAlloc(jsonPool_t *pool)
{
	jsonStaticPool_t *spool = json_containerOf(pool, jsonStaticPool_t, pool);
	if (spool->nextFree >= spool->qty)
		return 0;
	return spool->mem + spool->nextFree++;
}

/** Checks whether an character belongs to set.
  * @param ch Character value to be checked.
  * @param set Set of characters. It is just a null-terminated string.
  * @return true or false there is membership or not. */
static bool
json_isOneOfThem(char ch, char const *set)
{
	while (*set != '\0')
		if (ch == *set++)
			return true;
	return false;
}

/** Increases a pointer while it points to a character that belongs to a set.
  * @param str The initial pointer value.
  * @param set Set of characters. It is just a null-terminated string.
  * @return The final pointer value or null pointer if the null character was found. */
static char *
json_goWhile(char *str, char const *set)
{
	for (; *str != '\0'; ++str) {
		if (!json_isOneOfThem(*str, set))
			return str;
	}
	return 0;
}

/** Increases a pointer while it points to a white space character.
  * @param str The initial pointer value.
  * @return The final pointer value or null pointer if the null character was found. */
static char *
json_goBlank(char *str)
{
	return json_goWhile(str, json_blank);
}

/** Get a special character with its escape character. Examples:
  * 'b' -> '\\b', 'n' -> '\\n', 't' -> '\\t'
  * @param ch The escape character.
  * @retval  The character code. */
static char
json_getEscape(char ch)
{
	static struct {
		char ch;
		char code;
	} const pair[] = {
		{ '\"', '\"' },
		{ '\\', '\\' },
		{ '/', '/' },
		{ 'b', '\b' },
		{ 'f', '\f' },
		{ 'n', '\n' },
		{ 'r', '\r' },
		{ 't', '\t' },
	};
	unsigned int i;
	for (i = 0; i < sizeof pair / sizeof *pair; ++i)
		if (pair[i].ch == ch)
			return pair[i].code;
	return '\0';
}

/** Parse 4 characters.
  * @param str Pointer to  first digit.
  * @retval '?' If the four characters are hexadecimal digits.
  * @retval '\0' In other cases. */
static unsigned char
json_getCharFromUnicode(unsigned char const *str)
{
	unsigned int i;
	for (i = 0; i < 4; ++i)
		if (!isxdigit(str[i]))
			return '\0';
	return '?';
}

/** Parse a string and replace the scape characters by their meaning characters.
  * This parser stops when finds the character '\"'. Then replaces '\"' by '\0'.
  * @param str Pointer to first character.
  * @retval Pointer to first non white space after the string. If success.
  * @retval Null pointer if any error occur. */
static char *
json_parseString(char *str)
{
	unsigned char *head = (unsigned char *)str;
	unsigned char *tail = (unsigned char *)str;
	for (; *head; ++head, ++tail) {
		if (*head == '\"') {
			*tail = '\0';
			return (char *)++head;
		}
		if (*head == '\\') {
			if (*++head == 'u') {
				char const ch = json_getCharFromUnicode(++head);
				if (ch == '\0')
					return 0;
				*tail = ch;
				head += 3;
			} else {
				char const esc = json_getEscape(*head);
				if (esc == '\0')
					return 0;
				*tail = esc;
			}
		} else
			*tail = *head;
	}
	return 0;
}

/** Parse a string to get the name of a property.
  * @param ptr Pointer to first character.
  * @param property The property to assign the name.
  * @retval Pointer to first of property value. If success.
  * @retval Null pointer if any error occur. */
static char *
json_propertyName(char *ptr, json_t *property)
{
	property->name = ++ptr;
	ptr = json_parseString(ptr);
	if (!ptr)
		return 0;
	ptr = json_goBlank(ptr);
	if (!ptr)
		return 0;
	if (*ptr++ != ':')
		return 0;
	return json_goBlank(ptr);
}

/** Add a property to a JSON object or array.
  * @param obj The handler of the JSON object or array.
  * @param property The handler of the property to be added. */
static void
json_add(json_t *obj, json_t *property)
{
	property->sibling = 0;
	if (!obj->u.c.child) {
		obj->u.c.child = property;
		obj->u.c.last_child = property;
	} else {
		obj->u.c.last_child->sibling = property;
		obj->u.c.last_child = property;
	}
}

/** Parse a string to get the value of a property when its type is JSON_TEXT.
  * @param ptr Pointer to first character ('\"').
  * @param property The property to assign the name.
  * @retval Pointer to first non white space after the string. If success.
  * @retval Null pointer if any error occur. */
static char *
json_textValue(char *ptr, json_t *property)
{
	++property->u.value;
	ptr = json_parseString(++ptr);
	if (!ptr)
		return 0;
	property->type = JSON_TEXT;
	return ptr;
}

/** Compare two strings until get the null character in the second one.
  * @param ptr sub string
  * @param str main string
  * @retval Pointer to next character.
  * @retval Null pointer if any error occur. */
static char *
json_checkStr(char *ptr, char const *str)
{
	while (*str)
		if (*ptr++ != *str++)
			return 0;
	return ptr;
}

/** Set a char to '\0' and increase its pointer if the char is different to '}' or ']'.
  * @param ch Pointer to character.
  * @return  Final value pointer. */
static char *
json_setToNull(char *ch)
{
	if (!json_isOneOfThem(*ch, json_endofblock))
		*ch++ = '\0';
	return ch;
}

/** Indicate if a character is the end of a primitive value. */
static bool
json_isEndOfPrimitive(char ch)
{
	return ch == ',' || json_isOneOfThem(ch, json_blank) || json_isOneOfThem(ch, json_endofblock);
}

/** Parser a string to get a primitive value.
  * If the first character after the value is different of '}' or ']' is set to '\0'.
  * @param ptr Pointer to first character.
  * @param property Property handler to set the value and the type, (true, false or null).
  * @param value String with the primitive literal.
  * @param type The code of the type. ( JSON_BOOLEAN or JSON_NULL )
  * @retval Pointer to first non white space after the string. If success.
  * @retval Null pointer if any error occur. */
static char *
json_primitiveValue(char *ptr, json_t *property, char const *value, jsonType_t type)
{
	ptr = json_checkStr(ptr, value);
	if (!ptr || !json_isEndOfPrimitive(*ptr))
		return 0;
	ptr = json_setToNull(ptr);
	property->type = type;
	return ptr;
}

/** Parser a string to get a true value.
  * If the first character after the value is different of '}' or ']' is set to '\0'.
  * @param ptr Pointer to first character.
  * @param property Property handler to set the value and the type, (true, false or null).
  * @retval Pointer to first non white space after the string. If success.
  * @retval Null pointer if any error occur. */
static char *
json_trueValue(char *ptr, json_t *property)
{
	return json_primitiveValue(ptr, property, "true", JSON_BOOLEAN);
}

/** Parser a string to get a false value.
  * If the first character after the value is different of '}' or ']' is set to '\0'.
  * @param ptr Pointer to first character.
  * @param property Property handler to set the value and the type, (true, false or null).
  * @retval Pointer to first non white space after the string. If success.
  * @retval Null pointer if any error occur. */
static char *
json_falseValue(char *ptr, json_t *property)
{
	return json_primitiveValue(ptr, property, "false", JSON_BOOLEAN);
}

/** Parser a string to get a null value.
  * If the first character after the value is different of '}' or ']' is set to '\0'.
  * @param ptr Pointer to first character.
  * @param property Property handler to set the value and the type, (true, false or null).
  * @retval Pointer to first non white space after the string. If success.
  * @retval Null pointer if any error occur. */
static char *
json_nullValue(char *ptr, json_t *property)
{
	return json_primitiveValue(ptr, property, "null", JSON_NULL);
}

/** Parser a string to get a json object value.
  * @param ptr Pointer to first character.
  * @param obj The handler of the JSON root object or array.
  * @param pool The handler of a json pool for creating json instances.
  * @retval Pointer to first character after the value. If success.
  * @retval Null pointer if any error occur. */
/** Increases a pointer while it points to a decimal digit character.
  * @param str The initial pointer value.
  * @return The final pointer value or null pointer if the null character was found. */
static char *
json_goNum(char *str)
{
	for (; *str != '\0'; ++str) {
		if (!isdigit((int)(*str)))
			return str;
	}
	return 0;
}

/** Analyze the exponential part of a real number.
  * @param ptr Pointer to first character.
  * @retval Pointer to first non numerical after the string. If success.
  * @retval Null pointer if any error occur. */
static char *
json_expValue(char *ptr)
{
	if (*ptr == '-' || *ptr == '+')
		++ptr;
	if (!isdigit((int)(*ptr)))
		return 0;
	ptr = json_goNum(++ptr);
	return ptr;
}

/** Analyze the decimal part of a real number.
  * @param ptr Pointer to first character.
  * @retval Pointer to first non numerical after the string. If success.
  * @retval Null pointer if any error occur. */
static char *
json_fraqValue(char *ptr)
{
	if (!isdigit((int)(*ptr)))
		return 0;
	ptr = json_goNum(++ptr);
	if (!ptr)
		return 0;
	return ptr;
}

/** Parser a string to get a numerical value.
  * If the first character after the value is different of '}' or ']' is set to '\0'.
  * @param ptr Pointer to first character.
  * @param property Property handler to set the value and the type: JSON_REAL or JSON_INTEGER.
  * @retval Pointer to first non white space after the string. If success.
  * @retval Null pointer if any error occur. */
static char *
json_numValue(char *ptr, json_t *property)
{
	if (*ptr == '-')
		++ptr;
	if (!isdigit((int)(*ptr)))
		return 0;
	if (*ptr != '0') {
		ptr = json_goNum(ptr);
		if (!ptr)
			return 0;
	} else if (isdigit((int)(*++ptr)))
		return 0;
	property->type = JSON_INTEGER;
	if (*ptr == '.') {
		ptr = json_fraqValue(++ptr);
		if (!ptr)
			return 0;
		property->type = JSON_REAL;
	}
	if (*ptr == 'e' || *ptr == 'E') {
		ptr = json_expValue(++ptr);
		if (!ptr)
			return 0;
		property->type = JSON_REAL;
	}
	if (!json_isEndOfPrimitive(*ptr))
		return 0;
	if (JSON_INTEGER == property->type) {
		char const *value = property->u.value;
		bool const negative = *value == '-';
		static char const min[] = "-9223372036854775808";
		static char const max[] = "9223372036854775807";
		unsigned int const maxdigits = (negative ? sizeof min : sizeof max) - 1;
		unsigned int const len = (unsigned int const)(ptr - value);
		if (len > maxdigits)
			return 0;
		if (len == maxdigits) {
			char const tmp = *ptr;
			*ptr = '\0';
			char const *const threshold = negative ? min : max;
			if (0 > strcmp(threshold, value))
				return 0;
			*ptr = tmp;
		}
	}
	ptr = json_setToNull(ptr);
	return ptr;
}

static char *
json_objValue(char *ptr, json_t *obj, jsonPool_t *pool)
{
	obj->type = *ptr == '{' ? JSON_OBJ : JSON_ARRAY;
	obj->u.c.child = 0;
	obj->sibling = 0;
	ptr++;
	for (;;) {
		ptr = json_goBlank(ptr);
		if (!ptr)
			return 0;
		if (*ptr == ',') {
			++ptr;
			continue;
		}
		char const endchar = (obj->type == JSON_OBJ) ? '}' : ']';
		if (*ptr == endchar) {
			*ptr = '\0';
			json_t *parentObj = obj->sibling;
			if (!parentObj)
				return ++ptr;
			obj->sibling = 0;
			obj = parentObj;
			++ptr;
			continue;
		}
		json_t *property = pool->alloc(pool);
		if (!property)
			return 0;
		if (obj->type != JSON_ARRAY) {
			if (*ptr != '\"')
				return 0;
			ptr = json_propertyName(ptr, property);
			if (!ptr)
				return 0;
		} else
			property->name = 0;
		json_add(obj, property);
		property->u.value = ptr;
		switch (*ptr) {
		case '{':
			property->type = JSON_OBJ;
			property->u.c.child = 0;
			property->sibling = obj;
			obj = property;
			++ptr;
			break;
		case '[':
			property->type = JSON_ARRAY;
			property->u.c.child = 0;
			property->sibling = obj;
			obj = property;
			++ptr;
			break;
		case '\"': ptr = json_textValue(ptr, property); break;
		case 't': ptr = json_trueValue(ptr, property); break;
		case 'f': ptr = json_falseValue(ptr, property); break;
		case 'n': ptr = json_nullValue(ptr, property); break;
		default: ptr = json_numValue(ptr, property); break;
		}
		if (!ptr)
			return 0;
	}
}

/* Parse a string to get a json. */
json_t const *
json_createWithPool(char *str, jsonPool_t *pool)
{
	char *ptr = json_goBlank(str);
	if (!ptr || (*ptr != '{' && *ptr != '['))
		return 0;
	json_t *obj = pool->init(pool);
	obj->name = 0;
	obj->sibling = 0;
	obj->u.c.child = 0;
	ptr = json_objValue(ptr, obj, pool);
	if (!ptr)
		return 0;
	return obj;
}

/* Parse a string to get a json. */
json_t const *
json_create(char *str, json_t mem[], unsigned int qty)
{
	jsonStaticPool_t spool;
	spool.mem = mem;
	spool.qty = qty;
	spool.pool.init = json_poolInit;
	spool.pool.alloc = json_poolAlloc;
	return json_createWithPool(str, &spool.pool);
}

/*******************************************************************************
 * end tiny-json.c
 ******************************************************************************/

#define TINYJSON_MAX_FIELDS 2048

static bool
build_dict_from_json(struct workspace *wk, const json_t *json, obj *res)
{
	switch (json_getType(json)) {
	case JSON_OBJ:
		*res = make_obj(wk, obj_dict);
		for (const json_t *child = json_getChild(json); child; child = json_getSibling(child)) {
			obj val;

			if (!build_dict_from_json(wk, child, &val)) {
				return false;
			}

			obj key = make_str(wk, json_getName(child));
			obj_dict_set(wk, *res, key, val);
		}
		break;
	case JSON_ARRAY:
		*res = make_obj(wk, obj_array);
		for (const json_t *child = json_getChild(json); child; child = json_getSibling(child)) {
			obj val;

			if (!build_dict_from_json(wk, child, &val)) {
				return false;
			}

			obj_array_push(wk, *res, val);
		}
		break;
	case JSON_TEXT:
	/* muon doesn't have reals, so use string for the time being */
	case JSON_REAL: *res = make_str(wk, json_getValue(json)); break;
	case JSON_INTEGER:
		*res = make_obj(wk, obj_number);
		set_obj_number(wk, *res, (int64_t)json_getInteger(json));
		break;
	case JSON_NULL: *res = obj_null; break;
	case JSON_BOOLEAN: *res = make_obj_bool(wk, json_getBoolean(json)); break;
	default: LOG_E("error parsing json: invalid object"); return false;
	}

	return true;
}

bool
muon_json_to_dict(struct workspace *wk, char *json_str, obj *res)
{
	json_t mem[TINYJSON_MAX_FIELDS];

	const json_t *json = json_create(json_str, mem, TINYJSON_MAX_FIELDS);
	if (!json) {
		LOG_E("error parsing json to obj_dict: syntax error or out of memory");
		return false;
	}

	if (json_getType(json) != JSON_OBJ) {
		LOG_E("error parsing json to obj_dict: unexpected or invalid object");
		return false;
	}

	return build_dict_from_json(wk, json, res);
}
