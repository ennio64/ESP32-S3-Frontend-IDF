#ifndef KEYPAD_H
#define KEYPAD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void keypad_init();
char readKeypad();
void keypad_set_debounce(uint16_t ms);
void keypad_debug();
void keypad_reset(void);

#ifdef __cplusplus
}
#endif

#endif