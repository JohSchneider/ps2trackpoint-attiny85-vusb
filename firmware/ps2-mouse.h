#pragma once

#include <stdbool.h>

#define  ENABLE_WHEEL  0
#define  PS2_SAMPLES_PER_SEC  40  // 10..200

#if ENABLE_WHEEL
bool ps2mouse_wheel; // Protocol used: 0=no wheel, 1=with wheel
#endif
uint8_t ps2mouse_multiplier; // Scale coordinates
uint8_t ps2mouse_b; // Pressed buttons
int16_t ps2mouse_x, ps2mouse_y, ps2mouse_z; // Coordinates

void  ps2_init(void);
bool  ps2mouse_init(void);
bool  ps2mouse_process(void);
