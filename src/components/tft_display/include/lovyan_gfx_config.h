/**
 * @file lovyan_gfx_config.h
 * LovyanGFX configuration for ILI9341 on ESP32-S3
 * Used with LVGL as rendering backend
 */

#ifndef LOVYAN_GFX_CONFIG_H
#define LOVYAN_GFX_CONFIG_H

#include <LovyanGFX.hpp>
#include "handheld_config.h"
#include <driver/gpio.h>

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9341 _panel_instance;
    lgfx::Bus_SPI _bus_instance;

public:
    LGFX(void) {
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = SPI2_HOST;  // HSPI
            cfg.spi_mode = 0;
            cfg.freq_write = 20000000;  // 20MHz write
            cfg.freq_read = 10000000;   // 10MHz read
            cfg.pin_dc = TFT_DC;
            cfg.pin_mosi = TFT_MOSI;
            cfg.pin_miso = TFT_MISO;
            cfg.pin_sclk = TFT_SCLK;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs = TFT_CS;
            cfg.pin_rst = TFT_RST;
            cfg.panel_width = 240;   // Portrait width
            cfg.panel_height = 320;  // Portrait height
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;  // 0 degrees (portrait mode)
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;
            cfg.readable = true;
            cfg.invert = false;
            cfg.rgb_order = false;  // BGR order
            cfg.memory_width = 240;
            cfg.memory_height = 320;
            _panel_instance.config(cfg);
        }

        setPanel(&_panel_instance);
        
        // Setup backlight GPIO
        setupBacklight();
    }

private:
    void setupBacklight() {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << TFT_BL),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        gpio_config(&io_conf);
        gpio_set_level((gpio_num_t)TFT_BL, 0);  // Off initially
    }
};

#endif // LOVYAN_GFX_CONFIG_H
