#ifndef GLOBAL_SETTINGS_H
#define GLOBAL_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>
#include <nvs_flash.h>   // per nvs_handle_t

#ifdef __cplusplus
extern "C" {
#endif

// Inizializza il modulo settings (chiamare una volta all'avvio)
void settings_init(void);

// Legge un valore booleano
bool settings_get_bool(const char* key, bool default_val);
// Scrive un valore booleano
void settings_set_bool(const char* key, bool value);

// Legge un intero
int32_t settings_get_int(const char* key, int32_t default_val);
// Scrive un intero
void settings_set_int(const char* key, int32_t value);

// Legge un float
float settings_get_float(const char* key, float default_val);
// Scrive un float
void settings_set_float(const char* key, float value);

// Committ esplicita (opzionale, le scritture fanno commit automatico)
void settings_commit(void);

// Utility: toggle di un booleano con salvataggio e restart (opzionale)
void settings_toggle_and_restart(const char* key);

// Gestione tipo joystick (0 = analogico, 1 = digitale)
uint8_t settings_get_joystick_type(void);
void settings_set_joystick_type(uint8_t type);

// Impostazioni per il mandrino (comandi $37)
int settings_get_spindle_unlock_param(void);
void settings_set_spindle_unlock_param(int value);
int settings_get_spindle_lock_param(void);
void settings_set_spindle_lock_param(int value);

// Impostazioni per la visualizzazione in Spindle Positions
int settings_get_spindle_dir(void);
void settings_set_spindle_dir(int dir);
int settings_get_encoder_dir(void);
void settings_set_encoder_dir(int dir);

// Gestione modalità visualizzazione asse X (0 = diametro, 1 = raggio)
uint8_t settings_get_xmode(void);
void settings_set_xmode(uint8_t mode);

// Coefficiente profondità per filettatura esterna (default 1.0)
float settings_get_ext_depth_coeff(void);
void settings_set_ext_depth_coeff(float coeff);

// Coefficiente profondità per filettatura interna (default 1.0)
float settings_get_int_depth_coeff(void);
void settings_set_int_depth_coeff(float coeff);

// ========== Parametri PROBE ==========
float settings_get_probe_ring_outer_diameter(void);
void settings_set_probe_ring_outer_diameter(float diam);

float settings_get_probe_ring_inner_diameter(void);
void settings_set_probe_ring_inner_diameter(float diam);

float settings_get_probe_ring_thickness(void);
void settings_set_probe_ring_thickness(float thick);

float settings_get_probe_feed(void);
void settings_set_probe_feed(float feed);

float settings_get_probe_safety_dist(void);
void settings_set_probe_safety_dist(float dist);

// Posizione cambio utensile
void settings_get_tool_change_position(float *x, float *z);
void settings_set_tool_change_position(float x, float z);

// Accesso all'handle NVS per tool_database
nvs_handle_t settings_get_nvs_handle(void);

#ifdef __cplusplus
}
#endif

#endif // GLOBAL_SETTINGS_H