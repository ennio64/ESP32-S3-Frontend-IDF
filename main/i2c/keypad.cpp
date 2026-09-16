#include "keypad.h"
#include "i2c_scanner.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>

static const char *TAG = "KEYPAD";

// ======================================================
//   CLASSE C++ (driver 4x4 PCF8574) — VERSIONE STABILE
// ======================================================

class KeypadDriver {
public:
    KeypadDriver(uint8_t address, i2c_port_t port)
    : _address(address), _port(port), _debounce(0),
      _lastRead(0), _lastIdx(16), _dev(NULL) {}

    bool init();
    char read();
    void setDebounce(uint16_t ms) { _debounce = ms; }
    void reset() { _lastIdx = 16; _lastRead = 0; }
    void debug();

private:
    esp_err_t writeByte(uint8_t value);
    esp_err_t readByte(uint8_t mask, uint8_t &data);
    uint8_t getKeyIndex();

    uint8_t     _address;
    i2c_port_t  _port;
    uint16_t    _debounce;
    uint32_t    _lastRead;
    uint8_t     _lastIdx;

    i2c_master_dev_handle_t _dev;

    const char keymap[16] = {
        '1','4','7','*',
        '2','5','8','0',
        '3','6','9','#',
        'A','B','C','D'
    };
};

bool KeypadDriver::init()
{
    if (_dev) return true;

    i2c_master_bus_handle_t bus = get_i2c_bus();
    if (!bus) {
        ESP_LOGE(TAG, "❌ Bus I2C non disponibile");
        return false;
    }

    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = _address,
        .scl_speed_hz    = 50000,   // stabile per PCF8574
        .scl_wait_us     = 0,
        .flags           = 0
    };

    if (i2c_master_bus_add_device(bus, &cfg, &_dev) != ESP_OK) {
        ESP_LOGE(TAG, "❌ Impossibile aggiungere dispositivo keypad");
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(10)); // stabilizzazione

    uint8_t out = 0xFF;
    esp_err_t ret = i2c_master_transmit(_dev, &out, 1, pdMS_TO_TICKS(200));

    if (ret == ESP_OK)
        ESP_LOGI(TAG, "✅ Keypad inizializzato a 0x%02X", _address);
    else
        ESP_LOGW(TAG, "⚠ Keypad inizializzato ma risposta lenta: %s", esp_err_to_name(ret));

    return true;
}

esp_err_t KeypadDriver::writeByte(uint8_t value)
{
    return i2c_master_transmit(_dev, &value, 1, pdMS_TO_TICKS(200));
}

esp_err_t KeypadDriver::readByte(uint8_t mask, uint8_t &data)
{
    esp_err_t ret = writeByte(mask);
    if (ret != ESP_OK) return ret;

    vTaskDelay(pdMS_TO_TICKS(5));  // PCF8574 è lento

    ret = i2c_master_receive(_dev, &data, 1, pdMS_TO_TICKS(200));
    return ret;
}

uint8_t KeypadDriver::getKeyIndex()
{
    uint8_t key = 0;

    uint8_t rows;
    if (readByte(0xF0, rows) != ESP_OK) return 17;
    if (rows == 0xFF) return 17;
    if (rows == 0xF0) return 16;

    if      (rows == 0xE0) key = 0;
    else if (rows == 0xD0) key = 1;
    else if (rows == 0xB0) key = 2;
    else if (rows == 0x70) key = 3;
    else return 17;

    uint8_t cols;
    if (readByte(0x0F, cols) != ESP_OK) return 17;
    if (cols == 0xFF) return 17;
    if (cols == 0x0F) return 16;

    if      (cols == 0x0E) key += 0;
    else if (cols == 0x0D) key += 4;
    else if (cols == 0x0B) key += 8;
    else if (cols == 0x07) key += 12;
    else return 17;

    return key;
}

char KeypadDriver::read()
{
    if (!_dev && !init()) return 0;

    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    uint8_t idx = getKeyIndex();

    if (idx == 16) {
        _lastIdx = 16;
        return 0;
    }
    if (idx == 17) {
        ESP_LOGW(TAG, "⚠ Errore lettura keypad");
        return 0;
    }

    if (idx == _lastIdx) return 0;

    if (_debounce > 0 && (now - _lastRead) < _debounce)
        return 0;

    _lastIdx = idx;
    _lastRead = now;

    char key = keymap[idx];

    // ⭐ LOG DEL TASTO PREMUTO
    ESP_LOGI(TAG, "➡️  Tasto premuto: %c (idx=%d)", key, idx);

    return key;
}


void KeypadDriver::debug()
{
    ESP_LOGI(TAG, "=== DEBUG KEYPAD ===");
    while (true) {
        uint8_t idx = getKeyIndex();
        if (idx < 16)
            ESP_LOGI(TAG, "Idx=%d Char=%c", idx, keymap[idx]);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ======================================================
//   WRAPPER C — API IDENTICA AL TUO PROGRAMMA
// ======================================================

static KeypadDriver keypad(0x20, I2C_NUM_0);

extern "C" {

void keypad_init() {
    keypad.init();
}

char readKeypad() {
    return keypad.read();
}

void keypad_set_debounce(uint16_t ms) {
    keypad.setDebounce(ms);
}

void keypad_debug() {
    keypad.debug();
}

void keypad_reset(void) {
    keypad.reset();
}

}
