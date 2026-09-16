#include "ads1x15.h"
#include "i2c_scanner.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ADS1X15";

// Registri
#define ADS1X15_REG_CONVERT         0x00
#define ADS1X15_REG_CONFIG          0x01
#define ADS1X15_REG_LOW_THRESHOLD   0x02
#define ADS1X15_REG_HIGH_THRESHOLD  0x03

// Bit di configurazione
#define ADS1X15_OS_START_SINGLE     0x8000
#define ADS1X15_MODE_CONTINUE       0x0000
#define ADS1X15_MODE_SINGLE         0x0100

// Maschere per gain (PGA)
#define ADS1X15_PGA_6_144V          0x0000
#define ADS1X15_PGA_4_096V          0x0200
#define ADS1X15_PGA_2_048V          0x0400
#define ADS1X15_PGA_1_024V          0x0600
#define ADS1X15_PGA_0_512V          0x0800
#define ADS1X15_PGA_0_256V          0x0A00

// Comparator
#define ADS1X15_COMP_MODE_TRADITIONAL   0x0000
#define ADS1X15_COMP_MODE_WINDOW        0x0010
#define ADS1X15_COMP_POL_ACTIV_LOW      0x0000
#define ADS1X15_COMP_POL_ACTIV_HIGH     0x0008
#define ADS1X15_COMP_NON_LATCH          0x0000
#define ADS1X15_COMP_LATCH              0x0004
#define ADS1X15_COMP_QUE_NONE           0x0003

// Config flags per dispositivo
#define ADS_CONF_CHAN_1  0x00
#define ADS_CONF_CHAN_4  0x01
#define ADS_CONF_RES_12  0x00
#define ADS_CONF_RES_16  0x04
#define ADS_CONF_NOGAIN  0x00
#define ADS_CONF_GAIN    0x10
#define ADS_CONF_NOCOMP  0x00
#define ADS_CONF_COMP    0x20

struct ads1x15_t {
    uint8_t address;
    i2c_master_dev_handle_t i2c_dev;
    uint8_t config;
    uint8_t max_ports;
    uint8_t conv_delay_ms;
    uint8_t bit_shift;
    uint16_t gain_mask;
    uint16_t mode_mask;
    uint16_t datarate_mask;
    uint8_t comp_mode;
    uint8_t comp_pol;
    uint8_t comp_latch;
    uint8_t comp_que_convert;
    uint16_t last_request;
    int8_t error;
};

// Funzioni private
static bool write_reg(ads1x15_t *dev, uint8_t reg, uint16_t value) {
    uint8_t data[3] = { reg, (uint8_t)(value >> 8), (uint8_t)(value & 0xFF) };
    esp_err_t ret = i2c_master_transmit(dev->i2c_dev, data, 3, pdMS_TO_TICKS(200));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Scrittura registro 0x%02X fallita", reg);
        return false;
    }
    return true;
}

static bool read_reg(ads1x15_t *dev, uint8_t reg, uint16_t *value) {
    uint8_t data[2];
    esp_err_t ret = i2c_master_transmit_receive(dev->i2c_dev, &reg, 1, data, 2, pdMS_TO_TICKS(200));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Lettura registro 0x%02X fallita", reg);
        return false;
    }
    *value = (data[0] << 8) | data[1];
    return true;
}

static void request_adc_internal(ads1x15_t *dev, uint16_t readmode) {
    uint16_t config = ADS1X15_OS_START_SINGLE;
    config |= readmode;
    config |= dev->gain_mask;
    config |= dev->mode_mask;
    config |= dev->datarate_mask;
    config |= dev->comp_mode ? ADS1X15_COMP_MODE_WINDOW : ADS1X15_COMP_MODE_TRADITIONAL;
    config |= dev->comp_pol ? ADS1X15_COMP_POL_ACTIV_HIGH : ADS1X15_COMP_POL_ACTIV_LOW;
    config |= dev->comp_latch ? ADS1X15_COMP_LATCH : ADS1X15_COMP_NON_LATCH;
    config |= dev->comp_que_convert & 0x03;
    write_reg(dev, ADS1X15_REG_CONFIG, config);
    dev->last_request = readmode;
}

static int16_t read_adc_internal(ads1x15_t *dev, uint16_t readmode) {
    request_adc_internal(dev, readmode);
    if (dev->mode_mask == ADS1X15_MODE_SINGLE) {
        uint16_t config;
        int timeout = 50; // 50*2ms = 100ms max
        do {
            vTaskDelay(pdMS_TO_TICKS(2));
            if (!read_reg(dev, ADS1X15_REG_CONFIG, &config)) return 0;
            timeout--;
        } while (((config & 0x8000) == 0) && (timeout > 0));
        if (timeout == 0) {
            ESP_LOGW(TAG, "Timeout conversione");
            return 0;
        }
    } else {
        vTaskDelay(pdMS_TO_TICKS(dev->conv_delay_ms));
    }
    uint16_t value;
    if (!read_reg(dev, ADS1X15_REG_CONVERT, &value)) return 0;
    int16_t raw = (int16_t)value;
    if (dev->bit_shift) raw >>= dev->bit_shift;
    return raw;
}

// ==================================================
// Implementazione metodi pubblici
// ==================================================

ads1x15_t* ads1x15_new(uint8_t address, uint8_t max_ports, uint8_t conv_delay, uint8_t bit_shift, uint8_t config_flags) {
    ads1x15_t *dev = (ads1x15_t*)calloc(1, sizeof(ads1x15_t));
    if (!dev) return NULL;
    dev->address = address;
    dev->max_ports = max_ports;
    dev->conv_delay_ms = conv_delay;
    dev->bit_shift = bit_shift;
    dev->config = config_flags;
    dev->gain_mask = ADS1X15_PGA_2_048V;
    dev->mode_mask = ADS1X15_MODE_SINGLE;
    dev->datarate_mask = (4 << 5);
    dev->comp_mode = 0;
    dev->comp_pol = 1;
    dev->comp_latch = 0;
    dev->comp_que_convert = 3;
    dev->last_request = 0xFFFF;
    dev->error = ADS1X15_OK;
    return dev;
}

void ads1x15_free(ads1x15_t *dev) {
    if (dev) free(dev);
}

bool ads1x15_begin(ads1x15_t *dev) {
    i2c_master_bus_handle_t bus = get_i2c_bus();
    if (!bus) {
        ESP_LOGE(TAG, "Bus I2C non disponibile");
        return false;
    }
    i2c_device_config_t i2c_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dev->address,
        .scl_speed_hz = 50000,   // velocità ridotta per stabilità
        .scl_wait_us = 0,
        .flags = 0
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &i2c_cfg, &dev->i2c_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Impossibile aggiungere ADS1X15 a 0x%02X", dev->address);
        return false;
    }
    return true;
}

bool ads1x15_is_connected(ads1x15_t *dev) {
    uint8_t dummy;
    esp_err_t ret = i2c_master_transmit_receive(dev->i2c_dev, NULL, 0, &dummy, 0, pdMS_TO_TICKS(100));
    return (ret == ESP_OK);
}

void ads1x15_set_gain(ads1x15_t *dev, uint8_t gain) {
    if (!(dev->config & ADS_CONF_GAIN)) gain = 0;
    switch (gain) {
        case 0:  dev->gain_mask = ADS1X15_PGA_6_144V; break;
        case 1:  dev->gain_mask = ADS1X15_PGA_4_096V; break;
        case 2:  dev->gain_mask = ADS1X15_PGA_2_048V; break;
        case 4:  dev->gain_mask = ADS1X15_PGA_1_024V; break;
        case 8:  dev->gain_mask = ADS1X15_PGA_0_512V; break;
        case 16: dev->gain_mask = ADS1X15_PGA_0_256V; break;
        default: dev->gain_mask = ADS1X15_PGA_2_048V; break;
    }
}

uint8_t ads1x15_get_gain(ads1x15_t *dev) {
    switch (dev->gain_mask) {
        case ADS1X15_PGA_6_144V: return 0;
        case ADS1X15_PGA_4_096V: return 1;
        case ADS1X15_PGA_2_048V: return 2;
        case ADS1X15_PGA_1_024V: return 4;
        case ADS1X15_PGA_0_512V: return 8;
        case ADS1X15_PGA_0_256V: return 16;
        default: return ADS1X15_INVALID_GAIN;
    }
}

float ads1x15_to_voltage(ads1x15_t *dev, int16_t value) {
    float volts = ads1x15_get_max_voltage(dev);
    if (volts < 0) return volts;
    volts *= value;
    if (dev->config & ADS_CONF_RES_16)
        volts /= 32767.0f;
    else
        volts /= 2047.0f;
    return volts;
}

float ads1x15_get_max_voltage(ads1x15_t *dev) {
    switch (dev->gain_mask) {
        case ADS1X15_PGA_6_144V: return 6.144f;
        case ADS1X15_PGA_4_096V: return 4.096f;
        case ADS1X15_PGA_2_048V: return 2.048f;
        case ADS1X15_PGA_1_024V: return 1.024f;
        case ADS1X15_PGA_0_512V: return 0.512f;
        case ADS1X15_PGA_0_256V: return 0.256f;
        default: return ADS1X15_INVALID_VOLTAGE;
    }
}

void ads1x15_set_mode(ads1x15_t *dev, uint8_t mode) {
    dev->mode_mask = (mode == 0) ? ADS1X15_MODE_CONTINUE : ADS1X15_MODE_SINGLE;
}

uint8_t ads1x15_get_mode(ads1x15_t *dev) {
    return (dev->mode_mask == ADS1X15_MODE_CONTINUE) ? 0 : 1;
}

void ads1x15_set_data_rate(ads1x15_t *dev, uint8_t data_rate) {
    if (data_rate > 7) data_rate = 4;
    dev->datarate_mask = (data_rate << 5);
}

uint8_t ads1x15_get_data_rate(ads1x15_t *dev) {
    return (dev->datarate_mask >> 5) & 0x07;
}

int16_t ads1x15_read_adc(ads1x15_t *dev, uint8_t pin) {
    if (pin >= dev->max_ports) return 0;
    uint16_t mode = ((4 + pin) << 12);
    return read_adc_internal(dev, mode);
}

int16_t ads1x15_read_adc_differential_0_1(ads1x15_t *dev) {
    return read_adc_internal(dev, 0x0000);
}

int16_t ads1x15_get_value(ads1x15_t *dev) {
    uint16_t raw;
    if (!read_reg(dev, ADS1X15_REG_CONVERT, &raw)) return 0;
    int16_t value = (int16_t)raw;
    if (dev->bit_shift) value >>= dev->bit_shift;
    return value;
}

void ads1x15_request_adc(ads1x15_t *dev, uint8_t pin) {
    if (pin >= dev->max_ports) return;
    uint16_t mode = ((4 + pin) << 12);
    request_adc_internal(dev, mode);
}

void ads1x15_request_adc_differential_0_1(ads1x15_t *dev) {
    request_adc_internal(dev, 0x0000);
}

bool ads1x15_is_busy(ads1x15_t *dev) {
    return !ads1x15_is_ready(dev);
}

bool ads1x15_is_ready(ads1x15_t *dev) {
    uint16_t config;
    if (!read_reg(dev, ADS1X15_REG_CONFIG, &config)) return false;
    return (config & 0x8000) != 0;
}

uint8_t ads1x15_last_request(ads1x15_t *dev) {
    switch (dev->last_request) {
        case 0x4000: return 0x00;
        case 0x5000: return 0x01;
        case 0x6000: return 0x02;
        case 0x7000: return 0x03;
        case 0x0000: return 0x10;
        default: return 0xFF;
    }
}

void ads1x15_set_comparator_mode(ads1x15_t *dev, uint8_t mode) { dev->comp_mode = (mode != 0); }
uint8_t ads1x15_get_comparator_mode(ads1x15_t *dev) { return dev->comp_mode ? 1 : 0; }
void ads1x15_set_comparator_polarity(ads1x15_t *dev, uint8_t pol) { dev->comp_pol = (pol != 0); }
uint8_t ads1x15_get_comparator_polarity(ads1x15_t *dev) { return dev->comp_pol ? 1 : 0; }
void ads1x15_set_comparator_latch(ads1x15_t *dev, uint8_t latch) { dev->comp_latch = (latch != 0); }
uint8_t ads1x15_get_comparator_latch(ads1x15_t *dev) { return dev->comp_latch ? 1 : 0; }
void ads1x15_set_comparator_que_convert(ads1x15_t *dev, uint8_t mode) { dev->comp_que_convert = (mode < 3) ? mode : 3; }
uint8_t ads1x15_get_comparator_que_convert(ads1x15_t *dev) { return dev->comp_que_convert; }
void ads1x15_set_comparator_threshold_low(ads1x15_t *dev, int16_t lo) { write_reg(dev, ADS1X15_REG_LOW_THRESHOLD, (uint16_t)lo); }
int16_t ads1x15_get_comparator_threshold_low(ads1x15_t *dev) { uint16_t val; read_reg(dev, ADS1X15_REG_LOW_THRESHOLD, &val); return (int16_t)val; }
void ads1x15_set_comparator_threshold_high(ads1x15_t *dev, int16_t hi) { write_reg(dev, ADS1X15_REG_HIGH_THRESHOLD, (uint16_t)hi); }
int16_t ads1x15_get_comparator_threshold_high(ads1x15_t *dev) { uint16_t val; read_reg(dev, ADS1X15_REG_HIGH_THRESHOLD, &val); return (int16_t)val; }
int8_t ads1x15_get_error(ads1x15_t *dev) { int8_t err = dev->error; dev->error = ADS1X15_OK; return err; }

// Costruttori specifici
ads1x15_t* ads1015_new(uint8_t address) {
    return ads1x15_new(address, 4, 1, 4, ADS_CONF_COMP | ADS_CONF_GAIN | ADS_CONF_RES_12 | ADS_CONF_CHAN_4);
}

ads1x15_t* ads1115_new(uint8_t address) {
    return ads1x15_new(address, 4, 8, 0, ADS_CONF_COMP | ADS_CONF_GAIN | ADS_CONF_RES_16 | ADS_CONF_CHAN_4);
}

uint8_t ads1x15_to_percent(int16_t raw) {
    int32_t val = raw + 32768;
    if (val < 0) val = 0;
    if (val > 65535) val = 65535;
    return (uint8_t)(val / 655);   // 65535/100 = 655.35
}

void ads1x15_debug_task(void *pvParameters) {
    ads1x15_t *dev = (ads1x15_t*)pvParameters;
    if (!dev) {
        ESP_LOGE("ADS1X15", "Nessun device passato al task di debug");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI("ADS1X15", "=== Test ADS1115 (debug) ===");
    while (1) {
        int16_t x = ads1x15_read_adc(dev, 2);   // canale 2
        int16_t y = ads1x15_read_adc(dev, 3);   // canale 3
        uint8_t xp = ads1x15_to_percent(x);
        uint8_t yp = ads1x15_to_percent(y);
        ESP_LOGI("ADS1X15", "X=%6d (%3d%%)   Y=%6d (%3d%%)", x, xp, y, yp);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}