#include "tool_database.h"
#include "global_settings.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <cstdio>
#include <cstring>

static const char *TAG = "TOOL_DB";
#define NVS_KEY_TOOL_PREFIX "tool_"

static nvs_handle_t get_nvs() {
    return settings_get_nvs_handle();
}

static void get_tool_nvs_key(int tool, char *key, size_t key_len) {
    snprintf(key, key_len, "%s%d", NVS_KEY_TOOL_PREFIX, tool);
}

void tool_database_init(void) {
    ESP_LOGI(TAG, "Initializing tool database");
    char key[16];
    for (int t = 1; t <= MAX_TOOLS; ++t) {
        get_tool_nvs_key(t, key, sizeof(key));
        size_t size = 0;
        if (nvs_get_blob(get_nvs(), key, NULL, &size) != ESP_OK || size != sizeof(ToolData)) {
            ToolData empty;
            memset(&empty, 0, sizeof(empty));
            empty.calibrated = false;
            empty.configured = false;
            empty.type = TOOL_TYPE_EXTERNAL;
            empty.width = 0.0f;
            empty.feed = 0.0f;
            empty.rpm = 0;
            empty.material = TOOL_MATERIAL_STEEL;
            empty.image_name[0] = '\0';
            empty.tool_name[0] = '\0';          // <-- aggiunto
            empty.probe_type = PROBE_STANDARD;
            empty.hand = TOOL_HAND_RIGHT;
            nvs_set_blob(get_nvs(), key, &empty, sizeof(ToolData));
            ESP_LOGI(TAG, "Created empty tool %d", t);
        }
    }
    nvs_commit(get_nvs());
}

bool tool_exists(int tool) {
    return (tool >= 1 && tool <= MAX_TOOLS);
}

bool tool_is_associated(int tool) {
    ToolData data = tool_get_data(tool);
    return data.configured;
}

bool tool_is_calibrated(int tool) {
    ToolData data = tool_get_data(tool);
    return data.calibrated;
}

ToolData tool_get_data(int tool) {
    ToolData data;
    memset(&data, 0, sizeof(data));
    if (!tool_exists(tool)) return data;
    char key[16];
    get_tool_nvs_key(tool, key, sizeof(key));
    size_t size = sizeof(ToolData);
    esp_err_t err = nvs_get_blob(get_nvs(), key, &data, &size);
    if (err != ESP_OK || size != sizeof(ToolData)) {
        ESP_LOGW(TAG, "Failed to read tool %d, using zeros", tool);
    }
    return data;
}

void tool_set_data(int tool, const ToolData *data) {
    if (!tool_exists(tool) || !data) return;
    char key[16];
    get_tool_nvs_key(tool, key, sizeof(key));
    esp_err_t err = nvs_set_blob(get_nvs(), key, data, sizeof(ToolData));
    if (err == ESP_OK) {
        nvs_commit(get_nvs());
        ESP_LOGI(TAG, "Saved tool %d", tool);
    } else {
        ESP_LOGE(TAG, "Failed to save tool %d", tool);
    }
}

void tool_reset(int tool) {
    if (!tool_exists(tool)) return;
    ToolData empty;
    memset(&empty, 0, sizeof(empty));
    empty.calibrated = false;
    empty.configured = false;
    empty.type = TOOL_TYPE_EXTERNAL;
    empty.width = 0.0f;
    empty.feed = 0.0f;
    empty.rpm = 0;
    empty.material = TOOL_MATERIAL_STEEL;
    empty.image_name[0] = '\0';
    empty.tool_name[0] = '\0'; 
    empty.probe_type = PROBE_STANDARD;
    empty.hand = TOOL_HAND_RIGHT;
    tool_set_data(tool, &empty);
}

void tool_associate(int tool, const ToolData *catalog_data) {
    ToolData data = *catalog_data;
    data.calibrated = false;
    data.configured = true;
    tool_set_data(tool, &data);
}

float tool_get_offset_x(int tool) {
    return tool_get_data(tool).offset_x;
}

void tool_set_offset_x(int tool, float x) {
    ToolData data = tool_get_data(tool);
    data.offset_x = x;
    tool_set_data(tool, &data);
}

float tool_get_offset_z(int tool) {
    return tool_get_data(tool).offset_z;
}

void tool_set_offset_z(int tool, float z) {
    ToolData data = tool_get_data(tool);
    data.offset_z = z;
    tool_set_data(tool, &data);
}

void tool_save_all(void) {
    nvs_commit(get_nvs());
    ESP_LOGI(TAG, "All tools saved");
}