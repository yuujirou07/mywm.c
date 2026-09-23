#ifndef KEY_BORD_H
#define KEY_BORD_H

#include "pty_drawing.h"

void process_keyboard(struct windata *wd);
void key_callback(struct windata *wd, int key, bool repeat, bool control);
void character_callback(struct windata *wd, unsigned int codepoint);

#endif
