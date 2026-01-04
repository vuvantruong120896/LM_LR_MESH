#include "WiFiService.h"

#include "esp_mac.h"

void WiFiService::init() {
    uint8_t mac[6];
    // Read base MAC without bringing up the WiFi driver (important for low-power builds).
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    localAddress = (mac[4] << 8) | mac[5];
    ESP_LOGI(LM_TAG, "Local LoRa address (from WiFi MAC): %X", localAddress);
}

uint16_t WiFiService::getLocalAddress() {
    if (localAddress == 0)
        init();
    return localAddress;
}

uint16_t WiFiService::localAddress = 0;
