/* hint.h - Draw & Roll: the little pointing hand that "draws" a ramp when nobody has touched the screen for a while. */
#ifndef DR_HINT_H
#define DR_HINT_H

#include "util.h"

void hint_init(void);
void hint_shutdown(void);
/* any_down: a finger touched down this tick; any_active: at least one finger is on the screen */
void hint_update(bool any_down, bool any_active);
void hint_render(void);
void hint_skip_to(float idle_seconds);      /* e.g. after the sponge: show the hint again soon */

#endif
