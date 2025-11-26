#ifndef _BUTTON_CONTROL_H
#define _BUTTON_CONTROL_H

#include <Arduino.h>

// Default button configuration (can be overridden by app_config.h)
#ifndef BOARD_BUTTON
#define BOARD_BUTTON    3  // GPIO3 for handheld (can override in config)
#endif

#ifndef BUTTON_PRESSED
#define BUTTON_PRESSED  LOW  // Active LOW (pull-up, typical for ESP32)
#endif

#ifndef BUTTON_DEBOUNCE_MS
#define BUTTON_DEBOUNCE_MS  50
#endif

// Button event types
typedef enum {
    BUTTON_EVENT_NONE = 0,
    BUTTON_EVENT_PRESS,
    BUTTON_EVENT_RELEASE,
    BUTTON_EVENT_CLICK,
    BUTTON_EVENT_DOUBLE_CLICK,
    BUTTON_EVENT_LONG_PRESS
} button_event_t;

// Button callback function type
typedef void (*button_callback_t)(button_event_t event);

// Button control functions
void button_init();
void button_init_with_callback(button_callback_t callback);
void button_set_callback(button_callback_t callback);
void button_task(void* parameter);  // FreeRTOS task
void button_update();  // Call in main loop if not using task
bool button_is_pressed();
button_event_t button_get_last_event();

// Button timing configuration
void button_set_debounce_time(uint32_t ms);
void button_set_long_press_time(uint32_t ms);
void button_set_double_click_time(uint32_t ms);

#endif // _BUTTON_CONTROL_H