#include "i2c_scanner.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "I2C_SCAN";
static i2c_master_bus_handle_t i2c_bus_handle = NULL;

static void i2c_bus_init(void)
{
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = 0;
    bus_cfg.sda_io_num = GPIO_NUM_1;
    bus_cfg.scl_io_num = GPIO_NUM_2;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &i2c_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "I2C bus ready (SDA=GPIO1, SCL=GPIO2)");
}

static void i2c_scan_task(void *pvParams)
{
    i2c_bus_init();
    if (!i2c_bus_handle) {
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Scanning I2C bus...");
    int found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        // Timeout 200 ms per rilevare dispositivi lenti
        esp_err_t ret = i2c_master_probe(i2c_bus_handle, addr, pdMS_TO_TICKS(200));
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "✅ Device at 0x%02X", addr);
            found++;
        }
        vTaskDelay(pdMS_TO_TICKS(5)); // pausa breve
    }
    if (found == 0) {
        ESP_LOGI(TAG, "No I2C devices found");
    } else {
        ESP_LOGI(TAG, "Scan complete, found %d device(s)", found);
    }
    vTaskDelete(NULL);
}

void i2c_scan(void)
{
    // Avvia la scansione in background
    xTaskCreate(i2c_scan_task, "i2c_scan", 4096, NULL, 1, NULL);
}

i2c_master_bus_handle_t get_i2c_bus() {
    return i2c_bus_handle;
}