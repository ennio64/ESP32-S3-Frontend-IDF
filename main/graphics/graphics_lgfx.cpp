#include "graphics_lgfx.h"
#include "Colors.h"
#include "esp_log.h"

static LGFX_ILI9488 tft;

LGFX_ILI9488::LGFX_ILI9488()
{
    // Config SPI
    {
        auto cfg = _bus_instance.config();
        cfg.spi_host = SPI2_HOST;
        cfg.spi_mode = 0;
        cfg.freq_write = 27000000;
        cfg.freq_read  = 16000000;
        cfg.spi_3wire = false;
        cfg.use_lock = true;
        cfg.dma_channel = -1;
        cfg.pin_sclk = 14;
        cfg.pin_mosi = 13;
        cfg.pin_miso = -1;
        cfg.pin_dc   = 12;
        _bus_instance.config(cfg);
        _panel_instance.setBus(&_bus_instance);
    }
    // Config pannello
    {
        auto cfg = _panel_instance.config();
        cfg.pin_cs   = 10;
        cfg.pin_rst  = 9;
        cfg.pin_busy = -1;
        cfg.memory_width  = 320;
        cfg.memory_height = 480;
        cfg.panel_width   = 320;
        cfg.panel_height  = 480;
        cfg.offset_x = 0;
        cfg.offset_y = 0;
        cfg.offset_rotation = 0;
        cfg.dummy_read_pixel = 8;
        cfg.dummy_read_bits  = 1;
        cfg.readable = false;
        cfg.invert = false;
        cfg.rgb_order = false;
        cfg.dlen_16bit = false;
        cfg.bus_shared = false;
        _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);
    setRotation(3);
    setColorDepth(lgfx::rgb888_3Byte);
}

LGFX_ILI9488& get_tft() { return tft; }

void init_tft() {
    tft.init();
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(0, 0);
}

void tft_invert_display(bool invert) {
    get_tft().invertDisplay(invert);
}