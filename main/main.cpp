#include <stdio.h>
#include <sys/stat.h> // per stat()
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_spiffs.h"
#include "global_settings.h"
#include "global_vars.h"
#include "graphics_lgfx.h"
#include "graphics_primitive.h"
#include "graphics_helpers.h"
#include "image_manager.h"
#include "main_scene.h"
#include "cnc_manager.h"
#include "i2c_scanner.h"
#include "keypad.h"
#include "ADS1X15.h"
#include "BrakeAlert.h"

static const char *TAG = "MAIN";

void task_cnc(void *pvParameters)
{
    ESP_LOGI(TAG, "Task CNC avviato su core 0");
    setupCNC();
    while (true)
    {
        char *line;
        if (xQueueReceive(gcodeQueue, &line, 0) == pdTRUE)
        {
            cnc.sendCommand(line);
            free(line);
            vTaskDelay(pdMS_TO_TICKS(20)); // ritardo 20 ms dopo ogni riga inviata
        }
        cnc.update();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

extern "C" void app_main()
{
    esp_task_wdt_deinit();   // Disabilita completamente il task watchdog   
    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // SPIFFS (per i file PNG)
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true};
    ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SPIFFS mount failed (%s)", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "SPIFFS mounted");
    }

    // Verifica presenza file PNG
    struct stat st;
    if (stat("/spiffs/grblHAL_logo.png", &st) == 0)
    {
        ESP_LOGI(TAG, "✅ Logo centrale trovato, size %d bytes", st.st_size);
    }
    else
    {
        ESP_LOGE(TAG, "❌ Logo centrale NON trovato!");
    }
    if (stat("/spiffs/grblHAL_minilogo.png", &st) == 0)
    {
        ESP_LOGI(TAG, "✅ Mini logo trovato, size %d bytes", st.st_size);
    }
    else
    {
        ESP_LOGE(TAG, "❌ Mini logo NON trovato!");
    }

    // I2C bus e periferiche
    i2c_scan();
    keypad_init();
    keypad_set_debounce(200);

    // Display (inizializzazione preliminare)
    init_tft();

    // Impostazioni NVS (deve essere prima di leggere joystick_type)
    settings_init();

    // Legge il tipo di joystick (0=analogico, 1=digitale)
    int joystick_type = settings_get_int("joystick_type", 0);
    ESP_LOGI(TAG, "Joystick mode: %s", joystick_type == 0 ? "ANALOGICO" : "DIGITALE");

    // ADS1115
    ads1x15_t *ads = ads1115_new(0x48);
    ::ads = ads;
    if (ads1x15_begin(ads))
    {
        ads1x15_set_gain(ads, 1);
        ads1x15_set_mode(ads, 1);
        ESP_LOGI(TAG, "ADS1115 inizializzato");

        if (joystick_type == 0)
        {
            // Test analogico: canali 2 e 3 (assi X e Y)
            ESP_LOGI(TAG, "Test joystick analogico (canali 2 e 3):");
            for (int i = 0; i < 5; i++)
            {
                vTaskDelay(pdMS_TO_TICKS(100));
                int16_t x = ads1x15_read_adc(ads, 2);
                int16_t y = ads1x15_read_adc(ads, 3);
                ESP_LOGI(TAG, "X=%6d   Y=%6d", x, y);
            }
        }
        else
        {
            // Test digitale: tutti e 4 i canali (tasti direzionali)
            ESP_LOGI(TAG, "Test joystick digitale (canali 0-3):");
            for (int i = 0; i < 5; i++)
            {
                vTaskDelay(pdMS_TO_TICKS(100));
                int16_t ch0 = ads1x15_read_adc(ads, 0);
                int16_t ch1 = ads1x15_read_adc(ads, 1);
                int16_t ch2 = ads1x15_read_adc(ads, 2);
                int16_t ch3 = ads1x15_read_adc(ads, 3);
                ESP_LOGI(TAG, "CH0=%6d CH1=%6d CH2=%6d CH3=%6d", ch0, ch1, ch2, ch3);
            }
        }
    }
    else
    {
        ESP_LOGE(TAG, "ADS1115 non trovato");
    }

    // Applica eventuale invert del display (dipende dalle impostazioni NVS)
    bool invert = settings_get_bool("display_invert", false);
    tft_invert_display(invert);

    // Caricamento loghi e risorse grafiche
    preload_logo();
    drawLogoAtCenter();
    preload_miniLogo();
    displayReady = true;

    // Coda per comandi G-code
    gcodeQueue = xQueueCreate(10, sizeof(char *));

    // Avvia i task
    xTaskCreatePinnedToCore(task_cnc, "cnc", 8192, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(task_graphics, "graphics", 16384, NULL, 1, NULL, 1);

    ESP_LOGI(TAG, "Sistema avviato");
}

