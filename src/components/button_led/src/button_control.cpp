#include "button_control.h"

// Button state variables
static volatile bool button_current_state = false;
static volatile bool button_last_state = false;
static volatile uint32_t button_last_change_time = 0;
static volatile uint32_t button_press_start_time = 0;
static volatile uint32_t button_last_click_time = 0;
static volatile button_event_t button_last_event = BUTTON_EVENT_NONE;
static button_callback_t button_event_callback = nullptr;

// Timing configuration
static uint32_t debounce_time_ms = BUTTON_DEBOUNCE_MS;
static uint32_t long_press_time_ms = 1000;     // Short long-press (1s)
static uint32_t extended_press_time_ms = 5000; // Extended long-press (5s)
static uint32_t double_click_time_ms = 300;    // 300ms window for double click

// Internal state tracking
static bool waiting_for_double_click = false;
static bool short_long_press_triggered = false;
static bool extended_long_press_triggered = false;

void IRAM_ATTR button_isr() {
    uint32_t now = millis();
    bool current_reading = (digitalRead(BOARD_BUTTON) == BUTTON_PRESSED);
    
    // Debouncing: only update state if enough time has passed
    if (now - button_last_change_time > debounce_time_ms) {
        if (current_reading != button_current_state) {
            button_current_state = current_reading;
            button_last_change_time = now;
            
            if (current_reading) {
                // Button pressed (falling edge - GPIO goes LOW)
                button_press_start_time = now;
                short_long_press_triggered = false;
                extended_long_press_triggered = false;
                button_last_event = BUTTON_EVENT_PRESS;
                // NO LOGGING in ISR - avoid blocking
            } else {
                // Button released (rising edge - GPIO goes HIGH)
                button_last_event = BUTTON_EVENT_RELEASE;
                // NO LOGGING in ISR - avoid blocking
            }
        }
    }
}

void button_init() {
    pinMode(BOARD_BUTTON, INPUT_PULLUP);
    button_current_state = (digitalRead(BOARD_BUTTON) == BUTTON_PRESSED);
    button_last_state = button_current_state;
    
    // Attach interrupt for both rising and falling edges
    attachInterrupt(digitalPinToInterrupt(BOARD_BUTTON), button_isr, CHANGE);
}

void button_init_with_callback(button_callback_t callback) {
    button_init();
    button_event_callback = callback;
}

void button_set_callback(button_callback_t callback) {
    button_event_callback = callback;
}

void button_update() {
    uint32_t now = millis();
    static button_event_t last_processed_event = BUTTON_EVENT_NONE;
    
    // Process button events when state changes
    if (button_last_event != last_processed_event) {
        last_processed_event = button_last_event;
        
        if (button_last_event == BUTTON_EVENT_PRESS) {
            // Button just pressed - reset long-press tracking
            waiting_for_double_click = false;
            if (button_event_callback) {
                button_event_callback(BUTTON_EVENT_PRESS);
            }
        } 
        else if (button_last_event == BUTTON_EVENT_RELEASE) {
            // Button released - check if it was a long press
            if (short_long_press_triggered || extended_long_press_triggered) {
                // Long press was already triggered - don't generate click
                short_long_press_triggered = false;
                extended_long_press_triggered = false;
            } else {
                // Short tap - handle click/double-click detection
                if (waiting_for_double_click) {
                    // This is a double click
                    waiting_for_double_click = false;
                    if (button_event_callback) {
                        button_event_callback(BUTTON_EVENT_DOUBLE_CLICK);
                    }
                } else {
                    // Start waiting for potential double click
                    waiting_for_double_click = true;
                    button_last_click_time = now;
                }
            }
        }
    }
    
    // Monitor press duration for long-press detection
    if (button_current_state) {
        uint32_t press_duration = now - button_press_start_time;
        
        // Extended long press (5 seconds) - takes priority
        if (!extended_long_press_triggered && press_duration >= extended_press_time_ms) {
            extended_long_press_triggered = true;
            short_long_press_triggered = false;  // Don't trigger short press too
            waiting_for_double_click = false;
            if (button_event_callback) {
                button_event_callback(BUTTON_EVENT_EXTENDED_PRESS);  // Send extended event
            }
        }
        // Short long press (1 second) - only if 5s not yet triggered
        else if (!short_long_press_triggered && !extended_long_press_triggered && 
                 press_duration >= long_press_time_ms) {
            short_long_press_triggered = true;
            waiting_for_double_click = false;
            if (button_event_callback) {
                button_event_callback(BUTTON_EVENT_LONG_PRESS);  // Send short long-press event
            }
        }
    }
    
    // Handle single click timeout
    if (waiting_for_double_click && (now - button_last_click_time >= double_click_time_ms)) {
        waiting_for_double_click = false;
        if (button_event_callback) {
            button_event_callback(BUTTON_EVENT_CLICK);
        }
    }
}

void button_task(void* parameter) {
    for (;;) {
        button_update();
        vTaskDelay(pdMS_TO_TICKS(10));  // Check every 10ms
    }
}

bool button_is_pressed() {
    return button_current_state;
}

button_event_t button_get_last_event() {
    return button_last_event;
}

void button_set_debounce_time(uint32_t ms) {
    debounce_time_ms = ms;
}

void button_set_long_press_time(uint32_t ms) {
    long_press_time_ms = ms;
}

void button_set_extended_press_time(uint32_t ms) {
    extended_press_time_ms = ms;
}

void button_set_double_click_time(uint32_t ms) {
    double_click_time_ms = ms;
}