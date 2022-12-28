#ifndef MUON_PLATFORM_WINDOWS_GETOPT_H
#define MUON_PLATFORM_WINDOWS_GETOPT_H
int getopt(int, char * const [], const char *);
extern char *optarg;
extern int optind, opterr, optopt;
#endif
