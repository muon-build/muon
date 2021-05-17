#include "interpreter.h"
#include "log.h"
#include "parser.h"

#include <stdlib.h>
#include <string.h>

char *
object_to_str(struct object *obj)
{
	char *data = NULL;
	if (obj->type == OBJECT_TYPE_STRING) {
		data = calloc(obj->string.n, sizeof(char));
		strncpy(data, obj->string.data, obj->string.n);
	}

	return data;
}

struct object *
eval_meson_object(struct context *ctx, struct ast_function *function)
{
	struct ast_identifier *id = function->left;

	struct object *obj = NULL;

	if (strcmp(id->data, "project_version") == 0) {
		obj = calloc(1, sizeof(struct object));
		if (!obj) {
			LOG_W(log_misc, "failed to allocate string object");
		}

		obj->string.data = calloc(ctx->version.n, sizeof(char));
		strncpy(obj->string.data, ctx->version.data, ctx->version.n);
		obj->string.n = ctx->version.n;

		obj->type = OBJECT_TYPE_STRING;
	}

	return obj;
}

