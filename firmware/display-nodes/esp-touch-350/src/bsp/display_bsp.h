#pragma once

#include <Arduino.h>
#include <lvgl.h>

void display_init();
void display_set_backlight(uint8_t brightness_pct);
void display_toggle_rotation();
bool display_is_rotation_inverted();
