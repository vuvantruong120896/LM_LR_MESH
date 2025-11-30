#include "button_control.h"
#include <esp_log.h>

static const char* TAG = "Button";

// Button state variables
static volatile bool button_current_state = false;
static volatile bool button_last_state = false;
static volatile uint32_t button_last_change_time = 0;
static volatile uint32_t button_press_start_time = 0;
static volatile uint32_t button_release_time = 0;
static volatile uint32_t button_press_id = 0;  // Unique ID for each press session
static volatile button_event_t button_pending_event = BUTTON_EVENT_NONE;
static volatile bool button_event_ready = false;
static button_callback_t button_event_callback = nullptr;

// Timing configuration
static uint32_t debounce_time_ms = BUTTON_DEBOUNCE_MS;
static uint32_t long_press_time_ms = 1000;     // Short long-press (1s)
static uint32_t extended_press_time_ms = 5000; // Extended long-press (5s)
static uint32_t min_press_interval_ms = 100;   // Minimum interval between press sessions

// Internal state tracking for current press session
static volatile uint32_t current_press_id = 0;
static volatile bool long_press_triggered = false;
static volatile bool extended_press_triggered = false;
static volatile bool click_generated = false;

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
                // Only start new press session if enough time has passed since last release
                if (now - button_release_time >= min_press_interval_ms) {
                    button_press_start_time = now;
                    button_press_id++;  // New unique press session
                    button_pending_event = BUTTON_EVENT_PRESS;
                    button_event_ready = true;
                }
            } else {
                // Button released (rising edge - GPIO goes HIGH)
                button_release_time = now;
                button_pending_event = BUTTON_EVENT_RELEASE;
                button_event_ready = true;
            }
        }
    }
}

void button_init() {
    pinMode(BOARD_BUTTON, INPUT_PULLUP);
    button_current_state = (digitalRead(BOARD_BUTTON) == BUTTON_PRESSED);
    button_last_state = button_current_state;
    
    // Reset all state flags
    button_press_id = 0;
    current_press_id = 0;
    long_press_triggered = false;
    extended_press_triggered = false;
    click_generated = false;
    button_event_ready = false;
    button_pending_event = BUTTON_EVENT_NONE;
    button_press_start_time = 0;
    button_release_time = 0;
    
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
    
    // Process pending ISR events
    if (button_event_ready) {
        button_event_ready = false;
        button_event_t current_event = button_pending_event;
        
        if (current_event == BUTTON_EVENT_PRESS) {
            // Button just pressed - start new session
            current_press_id = button_press_id;
            long_press_triggered = false;
            extended_press_triggered = false;
            click_generated = false;
            
            ESP_LOGI(TAG, "🔘 PRESS detected (session=%lu, time=%lu)", current_press_id, button_press_start_time);
            if (button_event_callback) {
                button_event_callback(BUTTON_EVENT_PRESS);
            }
        } 
        else if (current_event == BUTTON_EVENT_RELEASE) {
            // Button released - check what kind of press it was
            uint32_t press_duration = now - button_press_start_time;
            
            ESP_LOGI(TAG, "🔘 RELEASE (session=%lu, duration=%lu ms, long=%d, extended=%d)", 
                     current_press_id, press_duration, long_press_triggered, extended_press_triggered);
            
            // Only generate click if this release matches current press session
            // and no long/extended press was triggered
            if (!long_press_triggered && !extended_press_triggered && !click_generated) {
                // Verify it's a genuine short press (not noise)
                if (press_duration >= debounce_time_ms && press_duration < long_press_time_ms) {
                    click_generated = true;
                    ESP_LOGI(TAG, "👆 CLICK triggered (duration=%lu ms)", press_duration);
                    if (button_event_callback) {
                        button_event_callback(BUTTON_EVENT_CLICK);
                    }
                } else if (press_duration < debounce_time_ms) {
                    ESP_LOGW(TAG, "   → Ignoring very short press (%lu ms < debounce)", press_duration);
                }
            } else {
                ESP_LOGI(TAG, "   → Long/Extended press was handled, no click");
            }
            
            // Reset session flags on release
            long_press_triggered = false;
            extended_press_triggered = false;
        }
    }
    
    // Monitor press duration for long-press detection (only while button is held)
    // Important: Only check if we're in a valid press session
    if (button_current_state && current_press_id == button_press_id && button_press_start_time > 0) {
        uint32_t press_duration = now - button_press_start_time;
        
        // Extended long press (5 seconds) - takes priority
        if (!extended_press_triggered && press_duration >= extended_press_time_ms) {
            extended_press_triggered = true;
            long_press_triggered = true;  // Also set this to prevent click on release
            ESP_LOGI(TAG, "🔴 EXTENDED_PRESS triggered (session=%lu, duration=%lu ms)", current_press_id, press_duration);
            if (button_event_callback) {
                button_event_callback(BUTTON_EVENT_EXTENDED_PRESS);
            }
        }
        // Short long press (1 second) - only if not already triggered
        else if (!long_press_triggered && !extended_press_triggered && press_duration >= long_press_time_ms) {
            long_press_triggered = true;
            ESP_LOGI(TAG, "🟠 LONG_PRESS triggered (session=%lu, duration=%lu ms)", current_press_id, press_duration);
            if (button_event_callback) {
                button_event_callback(BUTTON_EVENT_LONG_PRESS);
            }
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
    return button_pending_event;
}

void button_reset_state() {
    // Force reset all button state flags
    noInterrupts();  // Disable interrupts briefly for atomic reset
    long_press_triggered = false;
    extended_press_triggered = false;
    click_generated = false;
    button_event_ready = false;
    button_pending_event = BUTTON_EVENT_NONE;
    button_press_start_time = 0;
    current_press_id = 0;
    interrupts();  // Re-enable interrupts
    ESP_LOGI(TAG, "🔄 Button state reset");
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
    // Deprecated - no longer using double-click
}