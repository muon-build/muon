/*
 * SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "compat.h"

#include <string.h>

#include "arena.h"
#include "datastructures/stack.h"
#include "error.h"
#include "platform/assert.h"

/******************************************************************************
 * stack
 ******************************************************************************/

#define MUON_STACK_SANITIZE 1
#define MUON_STACK_DEBUG 0

#if MUON_STACK_SANITIZE
struct stack_tag {
	const char *name;
	uint32_t size;
};
#else
#include "log.h"
#endif

void
stack_init(struct arena *a, struct stack *stack, uint32_t cap)
{
	*stack = (struct stack){
		.mem = ar_alloc(a, cap, 1, 1),
		.cap = cap,
	};
}

static bool
stack_try_push_raw(struct stack *stack, const void *mem, uint32_t size)
{
	if (stack->len + size < stack->cap) {
		memcpy(stack->mem + stack->len, mem, size);
		stack->len += size;
		return true;
	}
	return false;
}

static void
stack_pop_raw(struct stack *stack, void *mem, uint32_t size)
{
	assert(stack->len >= size);
	stack->len -= size;
	memcpy(mem, stack->mem + stack->len, size);
}

static void
stack_peek_raw(struct stack *stack, void *mem, uint32_t size, uint32_t *off)
{
	assert(*off >= size);
	*off -= size;
	memcpy(mem, stack->mem + *off, size);
}

void
stack_print(struct stack *_stack)
{
#if MUON_STACK_SANITIZE
	struct stack_tag tag;
	struct stack stack = *_stack;
	while (stack.len) {
		stack_pop_raw(&stack, &tag, sizeof(tag));
		printf("  - %d, %04d - %s", stack.len, tag.size, tag.name);

		assert(stack.len >= tag.size);
		stack.len -= tag.size;

		if (tag.size == 1) {
			printf(" %02x", *(uint8_t *)(stack.mem + stack.len));
		}

		printf("\n");
	}
#else
	LOG_W("please enable MUON_STACK_SANITIZE\n");
#endif
}

bool
stack_try_push_sized(struct stack *stack, const void *mem, uint32_t size, const char *name)
{
	if (!stack_try_push_raw(stack, mem, size)) {
		return false;
	}
#if MUON_STACK_SANITIZE
	if (!stack_try_push_raw(stack, &(struct stack_tag){ name, size }, sizeof(struct stack_tag))) {
		return false;
	}

#if MUON_STACK_DEBUG
	L("\033[33mstack\033[0m %05d pushed %s (%d)", stack->len, name, size);
#endif
#endif
	return true;
}

void
stack_push_sized(struct stack *stack, const void *mem, uint32_t size, const char *name)
{
	if (!stack_try_push_sized(stack, mem, size, name)) {
		error_unrecoverable("stack overflow");
	}
}

void
stack_pop_sized(struct stack *stack, void *mem, uint32_t size)
{
#if MUON_STACK_SANITIZE
	struct stack_tag tag;
	stack_pop_raw(stack, &tag, sizeof(tag));
#if MUON_STACK_DEBUG
	if (size != tag.size) {
		LOG_E("size mismatch %d != %d (%s)", size, tag.size, tag.name);
	}
#endif
	assert(size == tag.size);
#endif

	stack_pop_raw(stack, mem, size);

#if MUON_STACK_SANITIZE
#if MUON_STACK_DEBUG
	L("\033[33mstack\033[0m %05d popped %s (%d)", stack->len, tag.name, tag.size);
#endif
#endif
}

void
stack_peek_sized(struct stack *stack, void *mem, uint32_t size)
{
	uint32_t off = stack->len;

#if MUON_STACK_SANITIZE
	struct stack_tag tag;
	stack_peek_raw(stack, &tag, sizeof(tag), &off);
	assert(size == tag.size);
#endif

	stack_peek_raw(stack, mem, size, &off);
}
