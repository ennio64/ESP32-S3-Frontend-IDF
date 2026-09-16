#include "image_manager.h"
#include "graphics_primitive.h"
#include "graphics_helpers.h"
#include "graphics_lgfx.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <cstdio>
#include <cstring>

static const char *TAG = "IMG_MGR";

// Buffer per immagini (statici, visibili solo in questo file)
static uint8_t* miniLogoBuffer = nullptr;
static size_t miniLogoSize = 0;

static uint8_t* centralLogoBuffer = nullptr;
static size_t centralLogoSize = 0;

static uint8_t* madreviteBuffer = nullptr;
static size_t madreviteSize = 0;
static uint8_t* viteBuffer = nullptr;
static size_t viteSize = 0;

static uint8_t* morseTaperBuffer = nullptr;
static size_t morseTaperSize = 0;
static uint8_t* mtABBuffer = nullptr;
static size_t mtABSize = 0;

// Nuovo buffer per l'immagine della scena Hemispherical
static uint8_t* hemisphericalBuffer = nullptr;
static size_t hemisphericalSize = 0;

// Helper privato per caricare un file in buffer (PSRAM o RAM)
static uint8_t* load_file_to_buffer(const char* path, size_t* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "File non trovato: %s", path);
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!buf) {
        buf = (uint8_t*)malloc(size);
        if (!buf) {
            ESP_LOGE(TAG, "Memoria insufficiente per %s", path);
            fclose(f);
            return nullptr;
        }
    }
    size_t read = fread(buf, 1, size, f);
    if (read != size) {
        ESP_LOGW(TAG, "Lettura incompleta di %s", path);
    }
    fclose(f);
    *out_size = size;
    ESP_LOGI(TAG, "Caricato %s (%d byte)", path, size);
    return buf;
}

// =============================================
// Precaricamento loghi
// =============================================
void preload_miniLogo(void) {
    if (miniLogoBuffer) return;
    miniLogoBuffer = load_file_to_buffer("/spiffs/grblHAL_minilogo.png", &miniLogoSize);
}

void preload_logo(void) {
    if (centralLogoBuffer) return;
    centralLogoBuffer = load_file_to_buffer("/spiffs/grblHAL_logo.png", &centralLogoSize);
}

void preloadTabThreadImages(void) {
    if (!madreviteBuffer) {
        madreviteBuffer = load_file_to_buffer("/spiffs/madrevite.png", &madreviteSize);
    }
    if (!viteBuffer) {
        viteBuffer = load_file_to_buffer("/spiffs/vite.png", &viteSize);
    }
}

void preloadTabTaperMorseImages(void) {
    if (!morseTaperBuffer) {
        morseTaperBuffer = load_file_to_buffer("/spiffs/MorseTaper.png", &morseTaperSize);
    }
    if (!mtABBuffer) {
        mtABBuffer = load_file_to_buffer("/spiffs/MT_A_vs_B.png", &mtABSize);
    }
}

// =============================================
// Precaricamento immagine per Hemispherical
// =============================================
void preload_hemispherical_image(void) {
    if (hemisphericalBuffer) return;
    hemisphericalBuffer = load_file_to_buffer("/spiffs/concave convex.png", &hemisphericalSize);
}

// =============================================
// Disegno loghi
// =============================================
void draw_miniLogoTopRight(int logoW, int logoH, int offsetX, int offsetY) {
    if (!miniLogoBuffer) {
        ESP_LOGW(TAG, "miniLogo non precaricato, uso placeholder");
        tft_fill_rect(480 - logoW - offsetX, offsetY, logoW, logoH, TFT_BLUE);
        return;
    }
    int posX = 480 - logoW - offsetX;
    int posY = offsetY;
    get_tft().drawPng(miniLogoBuffer, miniLogoSize, posX, posY);
}

void drawLogoAtCenter(int logoW, int logoH) {
    if (!centralLogoBuffer) {
        ESP_LOGW(TAG, "Logo centrale non precaricato, uso placeholder");
        int centerX = (480 - logoW) / 2;
        int centerY = (320 - logoH) / 2;
        tft_fill_rect(centerX, centerY, logoW, logoH, TFT_DARKGREY);
        tft_set_text_color(TFT_WHITE);
        tft_set_cursor(centerX + logoW/2 - 30, centerY + logoH/2 - 8);
        tft_print("GRBL");
        return;
    }
    int centerX = (480 - logoW) / 2;
    int centerY = (320 - logoH) / 2;
    get_tft().drawPng(centralLogoBuffer, centralLogoSize, centerX, centerY);
}

// =============================================
// Disegno immagini per tab (opzionali)
// =============================================
void draw_tabThreadImage(int x, int y, int w, int h, int type) {
    uint8_t* buf = nullptr;
    size_t size = 0;
    if (type == 0) {
        buf = madreviteBuffer;
        size = madreviteSize;
    } else {
        buf = viteBuffer;
        size = viteSize;
    }
    if (!buf) {
        tft_fill_rect(x, y, w, h, TFT_PURPLE);
        return;
    }
    get_tft().drawPng(buf, size, x, y);
}

void draw_tabTaperMorseImage(int x, int y, int w, int h, int type) {
    uint8_t* buf = nullptr;
    size_t size = 0;
    if (type == 0) {
        buf = morseTaperBuffer;
        size = morseTaperSize;
    } else {
        buf = mtABBuffer;
        size = mtABSize;
    }
    if (!buf) {
        tft_fill_rect(x, y, w, h, TFT_ORANGE);
        return;
    }
    get_tft().drawPng(buf, size, x, y);
}

// =============================================
// Disegno immagine per Hemispherical (centrata)
// =============================================
void draw_hemispherical_image(int x, int y, int imgW, int imgH) {
    if (!hemisphericalBuffer) {
        ESP_LOGW(TAG, "Immagine hemisferica non precaricata, uso placeholder");
        tft_fill_rect(x, y, imgW, imgH, TFT_DARKGREY);
        drawCenteredTextAt("(concave/convex)", x + imgW/2, y + imgH/2, 1, TFT_WHITE);
        return;
    }
    get_tft().drawPng(hemisphericalBuffer, hemisphericalSize, x, y);
}

/*#include "image_manager.h"
#include "graphics_primitive.h"
#include "graphics_lgfx.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <cstdio>
#include <cstring>

static const char *TAG = "IMG_MGR";

// Buffer per immagini (statici, visibili solo in questo file)
static uint8_t* miniLogoBuffer = nullptr;
static size_t miniLogoSize = 0;

static uint8_t* centralLogoBuffer = nullptr;
static size_t centralLogoSize = 0;

static uint8_t* madreviteBuffer = nullptr;
static size_t madreviteSize = 0;
static uint8_t* viteBuffer = nullptr;
static size_t viteSize = 0;

static uint8_t* morseTaperBuffer = nullptr;
static size_t morseTaperSize = 0;
static uint8_t* mtABBuffer = nullptr;
static size_t mtABSize = 0;

// Helper privato per caricare un file in buffer (PSRAM o RAM)
static uint8_t* load_file_to_buffer(const char* path, size_t* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "File non trovato: %s", path);
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!buf) {
        buf = (uint8_t*)malloc(size);
        if (!buf) {
            ESP_LOGE(TAG, "Memoria insufficiente per %s", path);
            fclose(f);
            return nullptr;
        }
    }
    size_t read = fread(buf, 1, size, f);
    if (read != size) {
        ESP_LOGW(TAG, "Lettura incompleta di %s", path);
    }
    fclose(f);
    *out_size = size;
    ESP_LOGI(TAG, "Caricato %s (%d byte)", path, size);
    return buf;
}

// =============================================
// Precaricamento loghi
// =============================================
void preload_miniLogo(void) {
    if (miniLogoBuffer) return;
    miniLogoBuffer = load_file_to_buffer("/spiffs/grblHAL_minilogo.png", &miniLogoSize);
}

void preload_logo(void) {
    if (centralLogoBuffer) return;
    centralLogoBuffer = load_file_to_buffer("/spiffs/grblHAL_logo.png", &centralLogoSize);
}

void preloadTabThreadImages(void) {
    if (!madreviteBuffer) {
        madreviteBuffer = load_file_to_buffer("/spiffs/madrevite.png", &madreviteSize);
    }
    if (!viteBuffer) {
        viteBuffer = load_file_to_buffer("/spiffs/vite.png", &viteSize);
    }
}

void preloadTabTaperMorseImages(void) {
    if (!morseTaperBuffer) {
        morseTaperBuffer = load_file_to_buffer("/spiffs/MorseTaper.png", &morseTaperSize);
    }
    if (!mtABBuffer) {
        mtABBuffer = load_file_to_buffer("/spiffs/MT_A_vs_B.png", &mtABSize);
    }
}

// =============================================
// Disegno loghi
// =============================================
void draw_miniLogoTopRight(int logoW, int logoH, int offsetX, int offsetY) {
    if (!miniLogoBuffer) {
        ESP_LOGW(TAG, "miniLogo non precaricato, uso placeholder");
        tft_fill_rect(480 - logoW - offsetX, offsetY, logoW, logoH, TFT_BLUE);
        return;
    }
    int posX = 480 - logoW - offsetX;
    int posY = offsetY;
    get_tft().drawPng(miniLogoBuffer, miniLogoSize, posX, posY);
}

void drawLogoAtCenter(int logoW, int logoH) {
    if (!centralLogoBuffer) {
        ESP_LOGW(TAG, "Logo centrale non precaricato, uso placeholder");
        int centerX = (480 - logoW) / 2;
        int centerY = (320 - logoH) / 2;
        tft_fill_rect(centerX, centerY, logoW, logoH, TFT_DARKGREY);
        tft_set_text_color(TFT_WHITE);
        tft_set_cursor(centerX + logoW/2 - 30, centerY + logoH/2 - 8);
        tft_print("GRBL");
        return;
    }
    int centerX = (480 - logoW) / 2;
    int centerY = (320 - logoH) / 2;
    get_tft().drawPng(centralLogoBuffer, centralLogoSize, centerX, centerY);
}

// =============================================
// Disegno immagini per tab (opzionali)
// =============================================
void draw_tabThreadImage(int x, int y, int w, int h, int type) {
    uint8_t* buf = nullptr;
    size_t size = 0;
    if (type == 0) {
        buf = madreviteBuffer;
        size = madreviteSize;
    } else {
        buf = viteBuffer;
        size = viteSize;
    }
    if (!buf) {
        tft_fill_rect(x, y, w, h, TFT_PURPLE);
        return;
    }
    get_tft().drawPng(buf, size, x, y);
}

void draw_tabTaperMorseImage(int x, int y, int w, int h, int type) {
    uint8_t* buf = nullptr;
    size_t size = 0;
    if (type == 0) {
        buf = morseTaperBuffer;
        size = morseTaperSize;
    } else {
        buf = mtABBuffer;
        size = mtABSize;
    }
    if (!buf) {
        tft_fill_rect(x, y, w, h, TFT_ORANGE);
        return;
    }
    get_tft().drawPng(buf, size, x, y);
}*/