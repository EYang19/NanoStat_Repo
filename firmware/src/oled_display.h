/*
 * oled_display.h
 * Small SSD1306/CFB display helper for NanoStat bring-up screens.
 */
#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

int oled_display_init(void);
bool oled_display_is_ready(void);
void oled_display_show_boot(const char *status);
void oled_display_show_afe_sample(uint32_t sample_count, int32_t current_pa);

#endif /* OLED_DISPLAY_H */
