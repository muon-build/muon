#ifndef MUON_EXTERNAL_SAMURAI_H
#define MUON_EXTERNAL_SAMURAI_H

#include <stdint.h>
#include <stdbool.h>

extern const bool have_samurai;

bool muon_samu(uint32_t argc, char *const argv[]);
bool muon_samu_compdb(const char *build, const char *compile_commands);
#endif
