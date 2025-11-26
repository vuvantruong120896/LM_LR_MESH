#include "display_task.h"
#include "display_manager.h"

TaskHandle_t DisplayTask::taskHandle = nullptr;

void DisplayTask::init() {
    // Task initialization if needed
}

void DisplayTask::stop() {
    if (taskHandle != nullptr) {
        vTaskDelete(taskHandle);
        taskHandle = nullptr;
    }
}

void DisplayTask::displayTaskFunction(void* param) {
    // Task function placeholder
    while (1) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}
