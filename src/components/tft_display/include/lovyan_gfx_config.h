#ifndef LOVYAN_GFX_CONFIG_H
#define LOVYAN_GFX_CONFIG_H

#include <LovyanGFX.hpp>
#include "handheld_config.h"
#include <driver/gpio.h>
#include <esp_log.h>

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9341 _panel_instance;
    lgfx::Bus_SPI _bus_instance;

public:
    LGFX(void) {
        {
            auto cfg = _bus_instance.config();
            
            // SPI Bus Configuration - from handheld_config.h
            cfg.spi_host = SPI2_HOST;  // HSPI
            cfg.spi_mode = 0;           // Mode 0
            cfg.freq_write = 20000000;  // 20MHz
            cfg.freq_read = 10000000;   // 10MHz for read
            cfg.pin_dc = TFT_DC;        // Use from handheld_config.h
            cfg.pin_mosi = TFT_MOSI;    // Use from handheld_config.h
            cfg.pin_miso = TFT_MISO;    // Use from handheld_config.h
            cfg.pin_sclk = TFT_SCLK;    // Use from handheld_config.h
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        {
            auto cfg = _panel_instance.config();
            
            // ILI9341 Panel Configuration - from handheld_config.h
            cfg.pin_cs = TFT_CS;        // Use from handheld_config.h
            cfg.pin_rst = TFT_RST;      // Use from handheld_config.h
            
            cfg.panel_width = 240;
            cfg.panel_height = 320;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 3;    // Rotation 180 degrees
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;
            cfg.readable = true;
            cfg.invert = false;
            cfg.rgb_order = false;      // RGB order (BGR)
            cfg.memory_width = 240;
            cfg.memory_height = 320;
            
            _panel_instance.config(cfg);
        }

        setPanel(&_panel_instance);
        
        // Setup backlight GPIO manually
        setupBacklight();
    }
    
private:
    void setupBacklight() {
        ESP_LOGI("LGFX", "Setting up backlight GPIO (pin %d)", TFT_BL);
        
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << TFT_BL),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io_conf);
        
        // Turn OFF backlight initially (will be turned on after display init)
        gpio_set_level((gpio_num_t)TFT_BL, 0);
        ESP_LOGI("LGFX", "Backlight turned OFF (will be turned on after display init)");
    }
};

#endif // LOVYAN_GFX_CONFIG_H
