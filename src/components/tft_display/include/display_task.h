#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Screen types
enum class DisplayScreen {
    HOME,          ///< Home screen
    SOIL_DATA,     ///< Soil sensor data
    DEVICE_CONFIG  ///< Device configuration
};

class DisplayTask {
public:
    static void init();
    static void stop();
    
private:
    static TaskHandle_t taskHandle;
    static void displayTaskFunction(void* param);
};
