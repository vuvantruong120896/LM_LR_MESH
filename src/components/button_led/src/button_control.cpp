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
static uint32_t long_press_time_ms = 1000;  // 1 second for long press
static uint32_t double_click_time_ms = 300;  // 300ms window for double click

// Internal state tracking
static bool waiting_for_double_click = false;
static bool long_press_triggered = false;

void IRAM_ATTR button_isr() {
    uint32_t now = millis();
    bool current_reading = (digitalRead(BOARD_BUTTON) == BUTTON_PRESSED);
    
    // Simple debouncing in ISR - more detailed processing in task/update
    if (now - button_last_change_time > debounce_time_ms) {
        if (current_reading != button_current_state) {
            button_current_state = current_reading;
            button_last_change_time = now;
            
            if (current_reading) {
                // Button pressed
                button_press_start_time = now;
                long_press_triggered = false;
                button_last_event = BUTTON_EVENT_PRESS;
            } else {
                // Button released
                button_last_event = BUTTON_EVENT_RELEASE;
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
    
    // Process button events
    if (button_last_event != last_processed_event) {
        last_processed_event = button_last_event;
        
        if (button_last_event == BUTTON_EVENT_PRESS) {
            // Reset double-click detection if we were waiting
            waiting_for_double_click = false;
            
            if (button_event_callback) {
                button_event_callback(BUTTON_EVENT_PRESS);
            }
        } 
        else if (button_last_event == BUTTON_EVENT_RELEASE && !long_press_triggered) {
            // Handle click and double-click detection
            if (waiting_for_double_click) {
                // This is a double click
                waiting_for_double_click = false;
                button_last_event = BUTTON_EVENT_DOUBLE_CLICK;
                
                if (button_event_callback) {
                    button_event_callback(BUTTON_EVENT_DOUBLE_CLICK);
                }
            } else {
                // Start waiting for potential double click
                waiting_for_double_click = true;
                button_last_click_time = now;
                
                if (button_event_callback) {
                    button_event_callback(BUTTON_EVENT_RELEASE);
                }
            }
        }
    }
    
    // Check for long press
    if (button_current_state && !long_press_triggered) {
        if (now - button_press_start_time >= long_press_time_ms) {
            long_press_triggered = true;
            button_last_event = BUTTON_EVENT_LONG_PRESS;
            waiting_for_double_click = false;  // Cancel double-click detection
            
            if (button_event_callback) {
                button_event_callback(BUTTON_EVENT_LONG_PRESS);
            }
        }
    }
    
    // Handle single click timeout
    if (waiting_for_double_click && (now - button_last_click_time >= double_click_time_ms)) {
        waiting_for_double_click = false;
        button_last_event = BUTTON_EVENT_CLICK;
        
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

void button_set_double_click_time(uint32_t ms) {
    double_click_time_ms = ms;
}