#ifndef SHARED_TYPES_ITERATOR_H
#define SHARED_TYPES_ITERATOR_H
enum iteration_result {
	ir_cont,
	ir_done,
	ir_err,
};

typedef enum iteration_result (*iterator_func)(void *ctx, void *val);
#endif

