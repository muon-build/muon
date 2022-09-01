#ifndef MUON_BUF_SIZE_H
#define MUON_BUF_SIZE_H

#define BUF_SIZE_S 255
#define BUF_SIZE_1k 1024
#define BUF_SIZE_2k 2048
#define BUF_SIZE_4k 4096
#define BUF_SIZE_32k (BUF_SIZE_4k * 8)
#define BUF_SIZE_1m 1048576ul

#define ARRAY_LEN(array) (sizeof(array) / sizeof(*array))
#endif
