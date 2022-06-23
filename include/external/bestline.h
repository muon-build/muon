#ifndef MUON_EXTERNAL_READLINE_H
#define MUON_EXTERNAL_READLINE_H

char *muon_bestline(const char *prompt);
void muon_bestline_free(const char *line);
int muon_bestline_history_add(const char *line);
void muon_bestline_history_free(void);
#endif
