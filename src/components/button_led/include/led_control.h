#ifndef _LED_CONTROL_H
#define _LED_CONTROL_H

#include <Arduino.h>

// Default LED configuration (can be overridden by app_config.h)
#ifndef BOARD_LED
#define BOARD_LED   0
#endif

#ifndef LED_ON
#define LED_ON      HIGH
#endif

#ifndef LED_OFF  
#define LED_OFF     LOW
#endif

// LED control functions
void led_init();
void led_on();
void led_off();
void led_toggle();
void led_flash(uint16_t flashes, uint16_t delayMs);
void led_pattern_startup();
void led_pattern_error();
void led_pattern_connected();
void led_pattern_message();

#endif // _LED_CONTROL_H