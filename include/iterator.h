#ifndef SHARED_TYPES_ITERATOR_H
#define SHARED_TYPES_ITERATOR_H
enum iteration_result {
	ir_cont,
	ir_done
};

typedef enum iteration_result (*iterator_func)(void *ctx, void *val);

enum del_iter_result {
	dir_cont,
	dir_break,
	dir_del,
};
#endif

