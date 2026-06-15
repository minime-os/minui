#ifndef MINIME_INPUT_H
#define MINIME_INPUT_H

#include <stddef.h>

int MINIME_inputOpenByName(const char *name);
int MINIME_inputOpenShortcutDevices(int *fds, size_t max_fds);
int MINIME_inputHasCZ(void);
int MINIME_inputNormalizeAxis(int value, int invert);

#endif
