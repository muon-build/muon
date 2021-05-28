/**
 * Copyright (c) 2017 rxi
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `microtar.c` for details.
 */

#ifndef MICROTAR_H
#define MICROTAR_H

#include <stdint.h>
#include <stdbool.h>

enum mtar_err {
	mtar_err_ok,
	mtar_err_badchksum,
	mtar_err_nullrecord,
	mtar_err_unknown_file_type,
};

enum mtar_file_type {
	mtar_file_type_reg,
	mtar_file_type_lnk,
	mtar_file_type_sym,
	mtar_file_type_chr,
	mtar_file_type_blk,
	mtar_file_type_dir,
	mtar_file_type_fifo,
};

struct mtar_header {
	uint32_t mode;
	uint32_t owner;
	uint32_t size;
	uint32_t mtime;
	enum mtar_file_type type;
	char name[100];
	char linkname[100];
	uint8_t *data;
};

struct mtar {
	uint8_t *data;
	uint32_t len;
	uint32_t off;
};

const char *mtar_strerror(enum mtar_err err);
const char *mtar_file_type_to_s(enum mtar_file_type type);
enum mtar_err mtar_read_header(struct mtar *tar, struct mtar_header *h);
#endif
