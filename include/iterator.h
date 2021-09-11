#ifndef MUON_ITERATOR_H
#define MUON_ITERATOR_H
enum iteration_result {
	ir_err,
	ir_cont,
	ir_done,
};

typedef enum iteration_result (*iterator_func)(void *ctx, void *val);
#endif

