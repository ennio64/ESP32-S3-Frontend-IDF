#ifndef ADS1X15_H
#define ADS1X15_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Costanti pubbliche (compatibili con la libreria originale)
#define ADS1X15_OK                        0
#define ADS1X15_INVALID_VOLTAGE           -100
#define ADS1X15_INVALID_GAIN              0xFF
#define ADS1X15_INVALID_MODE              0xFE

// Classe base ADS1X15 (astratta)
typedef struct ads1x15_t ads1x15_t;

// Costruttore (alloca e inizializza)
ads1x15_t* ads1x15_new(uint8_t address, uint8_t max_ports, uint8_t conv_delay, uint8_t bit_shift, uint8_t config_flags);

// Distruttore
void ads1x15_free(ads1x15_t *dev);

bool ads1x15_begin(ads1x15_t *dev);
bool ads1x15_is_connected(ads1x15_t *dev);

void ads1x15_set_gain(ads1x15_t *dev, uint8_t gain);
uint8_t ads1x15_get_gain(ads1x15_t *dev);

float ads1x15_to_voltage(ads1x15_t *dev, int16_t value);
float ads1x15_get_max_voltage(ads1x15_t *dev);

void ads1x15_set_mode(ads1x15_t *dev, uint8_t mode);   // 0=continuous, 1=single
uint8_t ads1x15_get_mode(ads1x15_t *dev);

void ads1x15_set_data_rate(ads1x15_t *dev, uint8_t data_rate);
uint8_t ads1x15_get_data_rate(ads1x15_t *dev);

int16_t ads1x15_read_adc(ads1x15_t *dev, uint8_t pin);
int16_t ads1x15_read_adc_differential_0_1(ads1x15_t *dev);
int16_t ads1x15_get_value(ads1x15_t *dev);

void ads1x15_request_adc(ads1x15_t *dev, uint8_t pin);
void ads1x15_request_adc_differential_0_1(ads1x15_t *dev);
bool ads1x15_is_busy(ads1x15_t *dev);
bool ads1x15_is_ready(ads1x15_t *dev);
uint8_t ads1x15_last_request(ads1x15_t *dev);

void ads1x15_set_comparator_mode(ads1x15_t *dev, uint8_t mode);
uint8_t ads1x15_get_comparator_mode(ads1x15_t *dev);
void ads1x15_set_comparator_polarity(ads1x15_t *dev, uint8_t pol);
uint8_t ads1x15_get_comparator_polarity(ads1x15_t *dev);
void ads1x15_set_comparator_latch(ads1x15_t *dev, uint8_t latch);
uint8_t ads1x15_get_comparator_latch(ads1x15_t *dev);
void ads1x15_set_comparator_que_convert(ads1x15_t *dev, uint8_t mode);
uint8_t ads1x15_get_comparator_que_convert(ads1x15_t *dev);
void ads1x15_set_comparator_threshold_low(ads1x15_t *dev, int16_t lo);
int16_t ads1x15_get_comparator_threshold_low(ads1x15_t *dev);
void ads1x15_set_comparator_threshold_high(ads1x15_t *dev, int16_t hi);
int16_t ads1x15_get_comparator_threshold_high(ads1x15_t *dev);

int8_t ads1x15_get_error(ads1x15_t *dev);

// Classi derivate specifiche (costruttori)
ads1x15_t* ads1015_new(uint8_t address);   // 4 canali, 12 bit, gain, comparator
ads1x15_t* ads1115_new(uint8_t address);   // 4 canali, 16 bit, gain, comparator

uint8_t ads1x15_to_percent(int16_t raw);
void ads1x15_debug_task(void *pvParameters);

// Opzionale: costruttori per ADS1013/1014/1113/1114 (se necessari)

#ifdef __cplusplus
}
#endif

#endif