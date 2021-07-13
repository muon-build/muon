#ifndef SHARED_TYPES_ITERATOR_H
#define SHARED_TYPES_ITERATOR_H
enum iteration_result {
	ir_err,
	ir_cont,
	ir_done,
};

typedef enum iteration_result (*iterator_func)(void *ctx, void *val);
#endif

