#include "led_control.h"

void led_init() {
    pinMode(BOARD_LED, OUTPUT);
    led_off();
}

void led_on() {
    digitalWrite(BOARD_LED, LED_ON);
}

void led_off() {
    digitalWrite(BOARD_LED, LED_OFF);
}

void led_toggle() {
    digitalWrite(BOARD_LED, !digitalRead(BOARD_LED));
}

void led_flash(uint16_t flashes, uint16_t delayMs) {
    for (uint16_t i = 0; i < flashes; i++) {
        led_on();
        delay(delayMs);
        led_off();
        delay(delayMs);
    }
}

void led_pattern_startup() {
    led_flash(3, 250);  // 3 quick flashes on startup
}

void led_pattern_error() {
    led_flash(5, 200);  // 5 slow flashes for error
}

void led_pattern_connected() {
    led_flash(3, 100);  // 3 quick flashes when connected
}

void led_pattern_message() {
    led_flash(1, 100);   // 1 very quick flash for message received
}