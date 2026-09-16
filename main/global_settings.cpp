#include "global_settings.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#include "user_config.h"

static const char* TAG = "SETTINGS";
static nvs_handle_t nvs_handle_storage = 0;

// Chiavi NVS per i parametri
#define PROBE_OUTER_DIAM_KEY "probe_outer_diam"
#define PROBE_INNER_DIAM_KEY "probe_inner_diam"
#define PROBE_RING_THICKNESS_KEY "probe_thick"
#define PROBE_FEED_KEY "probe_feed"
#define PROBE_SAFETY_KEY "probe_safety"
#define TOOL_CHANGE_X_KEY "toolch_x"
#define TOOL_CHANGE_Z_KEY "toolch_z"

void settings_init(void) {
    esp_err_t err = nvs_open("settings", NVS_READWRITE, &nvs_handle_storage);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Settings NVS initialized");

    // Inizializza il tipo di joystick solo se non esiste già
    int32_t tmp;
    if (nvs_get_i32(nvs_handle_storage, "joystick_type", &tmp) != ESP_OK) {
        nvs_set_i32(nvs_handle_storage, "joystick_type", JOYSTICK_TYPE_DEFAULT);
        nvs_commit(nvs_handle_storage);
        ESP_LOGI(TAG, "Joystick type initialized to %d", JOYSTICK_TYPE_DEFAULT);
    }

    // Inizializza parametri probe con valori di default da user_config.h
    float fval;
    size_t size = sizeof(float);
    if (nvs_get_blob(nvs_handle_storage, PROBE_OUTER_DIAM_KEY, &fval, &size) != ESP_OK) {
        float def = PROBE_RING_OUTER_DIAMETER_DEFAULT;
        nvs_set_blob(nvs_handle_storage, PROBE_OUTER_DIAM_KEY, &def, sizeof(def));
        ESP_LOGI(TAG, "Probe outer diameter initialized to %.1f", def);
    }
    if (nvs_get_blob(nvs_handle_storage, PROBE_INNER_DIAM_KEY, &fval, &size) != ESP_OK) {
        float def = PROBE_RING_INNER_DIAMETER_DEFAULT;
        nvs_set_blob(nvs_handle_storage, PROBE_INNER_DIAM_KEY, &def, sizeof(def));
        ESP_LOGI(TAG, "Probe inner diameter initialized to %.1f", def);
    }
    if (nvs_get_blob(nvs_handle_storage, PROBE_RING_THICKNESS_KEY, &fval, &size) != ESP_OK) {
        float def = PROBE_RING_THICKNESS_DEFAULT;
        nvs_set_blob(nvs_handle_storage, PROBE_RING_THICKNESS_KEY, &def, sizeof(def));
        ESP_LOGI(TAG, "Probe ring thickness initialized to %.1f", def);
    }
    if (nvs_get_blob(nvs_handle_storage, PROBE_FEED_KEY, &fval, &size) != ESP_OK) {
        float def = PROBE_FEED_DEFAULT;
        nvs_set_blob(nvs_handle_storage, PROBE_FEED_KEY, &def, sizeof(def));
        ESP_LOGI(TAG, "Probe feed initialized to %.1f", def);
    }
    if (nvs_get_blob(nvs_handle_storage, PROBE_SAFETY_KEY, &fval, &size) != ESP_OK) {
        float def = PROBE_SAFETY_DIST_DEFAULT;
        nvs_set_blob(nvs_handle_storage, PROBE_SAFETY_KEY, &def, sizeof(def));
        ESP_LOGI(TAG, "Probe safety distance initialized to %.1f", def);
    }

    nvs_commit(nvs_handle_storage);
}

bool settings_get_bool(const char* key, bool default_val) {
    if (!nvs_handle_storage) return default_val;
    uint8_t val;
    esp_err_t err = nvs_get_u8(nvs_handle_storage, key, &val);
    if (err != ESP_OK) return default_val;
    return val != 0;
}

void settings_set_bool(const char* key, bool value) {
    if (!nvs_handle_storage) return;
    esp_err_t err = nvs_set_u8(nvs_handle_storage, key, value ? 1 : 0);
    if (err == ESP_OK) {
        nvs_commit(nvs_handle_storage);
        ESP_LOGI(TAG, "Set %s = %d", key, value);
    } else {
        ESP_LOGE(TAG, "Failed to set %s", key);
    }
}

int32_t settings_get_int(const char* key, int32_t default_val) {
    if (!nvs_handle_storage) return default_val;
    int32_t val;
    esp_err_t err = nvs_get_i32(nvs_handle_storage, key, &val);
    return (err == ESP_OK) ? val : default_val;
}

void settings_set_int(const char* key, int32_t value) {
    if (!nvs_handle_storage) return;
    esp_err_t err = nvs_set_i32(nvs_handle_storage, key, value);
    if (err == ESP_OK) nvs_commit(nvs_handle_storage);
}

float settings_get_float(const char* key, float default_val) {
    if (!nvs_handle_storage) return default_val;
    float val = default_val;
    size_t len = sizeof(float);
    esp_err_t err = nvs_get_blob(nvs_handle_storage, key, &val, &len);
    if (err != ESP_OK || len != sizeof(float)) return default_val;
    return val;
}

void settings_set_float(const char* key, float value) {
    if (!nvs_handle_storage) return;
    esp_err_t err = nvs_set_blob(nvs_handle_storage, key, &value, sizeof(float));
    if (err == ESP_OK) nvs_commit(nvs_handle_storage);
}

void settings_commit(void) {
    if (nvs_handle_storage) nvs_commit(nvs_handle_storage);
}

void settings_toggle_and_restart(const char* key) {
    bool current = settings_get_bool(key, false);
    settings_set_bool(key, !current);
    ESP_LOGI(TAG, "Toggled %s to %d, restarting...", key, !current);
    esp_restart();
}

uint8_t settings_get_joystick_type(void) {
    return (uint8_t)settings_get_int("joystick_type", JOYSTICK_TYPE_DEFAULT);
}

void settings_set_joystick_type(uint8_t type) {
    settings_set_int("joystick_type", (int32_t)type);
}

uint8_t settings_get_xmode(void) {
    return (uint8_t)settings_get_int("xmode", X_MODE_DEFAULT);
}

void settings_set_xmode(uint8_t mode) {
    settings_set_int("xmode", (int32_t)mode);
}

int settings_get_spindle_unlock_param(void) {
    return settings_get_int("spindle_unlock", SPINDLE_UNLOCK_PARAM_DEFAULT);
}

void settings_set_spindle_unlock_param(int value) {
    if (value < 0) value = 0;
    settings_set_int("spindle_unlock", value);
}

int settings_get_spindle_lock_param(void) {
    return settings_get_int("spindle_lock", SPINDLE_LOCK_PARAM_DEFAULT);
}

void settings_set_spindle_lock_param(int value) {
    if (value < 0) value = 0;
    settings_set_int("spindle_lock", value);
}

int settings_get_spindle_dir(void) {
    return settings_get_int("spindle_dir", SPINDLE_DIR_DEFAULT);
}

void settings_set_spindle_dir(int dir) {
    if (dir < 0) dir = 0;
    if (dir > 1) dir = 1;
    settings_set_int("spindle_dir", dir);
}

int settings_get_encoder_dir(void) {
    return settings_get_int("encoder_dir", ENCODER_DIR_DEFAULT);
}

void settings_set_encoder_dir(int dir) {
    if (dir < 0) dir = 0;
    if (dir > 1) dir = 1;
    settings_set_int("encoder_dir", dir);
}

float settings_get_ext_depth_coeff(void) {
    return settings_get_float("ext_depth_coeff", EXT_DEPTH_COEFF_DEFAULT);
}

void settings_set_ext_depth_coeff(float coeff) {
    if (coeff < 0.5f) coeff = 0.5f;
    if (coeff > 2.0f) coeff = 2.0f;
    settings_set_float("ext_depth_coeff", coeff);
}

float settings_get_int_depth_coeff(void) {
    return settings_get_float("int_depth_coeff", INT_DEPTH_COEFF_DEFAULT);
}

void settings_set_int_depth_coeff(float coeff) {
    if (coeff < 0.5f) coeff = 0.5f;
    if (coeff > 2.0f) coeff = 2.0f;
    settings_set_float("int_depth_coeff", coeff);
}

// ========== Funzioni per sonda ad anello (nomi allineati con header) ==========

float settings_get_probe_ring_outer_diameter(void) {
    return settings_get_float(PROBE_OUTER_DIAM_KEY, PROBE_RING_OUTER_DIAMETER_DEFAULT);
}

void settings_set_probe_ring_outer_diameter(float diam) {
    if (diam <= 0) diam = 0.1f;
    settings_set_float(PROBE_OUTER_DIAM_KEY, diam);
}

float settings_get_probe_ring_inner_diameter(void) {
    return settings_get_float(PROBE_INNER_DIAM_KEY, PROBE_RING_INNER_DIAMETER_DEFAULT);
}

void settings_set_probe_ring_inner_diameter(float diam) {
    if (diam <= 0) diam = 0.1f;
    settings_set_float(PROBE_INNER_DIAM_KEY, diam);
}

float settings_get_probe_ring_thickness(void) {
    return settings_get_float(PROBE_RING_THICKNESS_KEY, PROBE_RING_THICKNESS_DEFAULT);
}

void settings_set_probe_ring_thickness(float thick) {
    if (thick <= 0) thick = 0.1f;
    settings_set_float(PROBE_RING_THICKNESS_KEY, thick);
}

float settings_get_probe_feed(void) {
    return settings_get_float(PROBE_FEED_KEY, PROBE_FEED_DEFAULT);
}

void settings_set_probe_feed(float feed) {
    if (feed <= 0) feed = 1.0f;
    settings_set_float(PROBE_FEED_KEY, feed);
}

float settings_get_probe_safety_dist(void) {
    return settings_get_float(PROBE_SAFETY_KEY, PROBE_SAFETY_DIST_DEFAULT);
}

void settings_set_probe_safety_dist(float dist) {
    if (dist < 0) dist = 0;
    settings_set_float(PROBE_SAFETY_KEY, dist);
}

void settings_get_tool_change_position(float *x, float *z) {
    if (x) *x = settings_get_float(TOOL_CHANGE_X_KEY, 0.0f);
    if (z) *z = settings_get_float(TOOL_CHANGE_Z_KEY, 0.0f);
}

void settings_set_tool_change_position(float x, float z) {
    settings_set_float(TOOL_CHANGE_X_KEY, x);
    settings_set_float(TOOL_CHANGE_Z_KEY, z);
}

nvs_handle_t settings_get_nvs_handle(void) {
    return nvs_handle_storage;
}