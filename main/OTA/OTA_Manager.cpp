#include "OTA_Manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_http_server.h"
#include "esp_now.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "keypad.h"
#include "graphics_lgfx.h"
#include "graphics_primitive.h"
#include <cstring>

static const char *TAG = "OTA_MANAGER";

#define OTA_SSID      "MyESPfamily"
#define OTA_PASSWORD  "12345678"
#define OTA_CHANNEL   1
#define OTA_MAX_CONN  4

#define OTA_LOCAL_IP   "192.168.5.1"
#define OTA_GATEWAY    "192.168.5.1"
#define OTA_NETMASK    "255.255.255.0"

static httpd_handle_t server = NULL;

static const char* OTA_HTML = 
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<meta charset='utf-8'>"
    "<title>ESP32 OTA Updater</title>"
    "<style>"
    "body{font-family:Arial;text-align:center;margin-top:50px}"
    ".container{max-width:500px;margin:auto;padding:20px;border:1px solid #ccc;border-radius:10px}"
    "input,button{margin:10px;padding:10px;font-size:16px}"
    ".status{color:green;margin-top:20px}"
    "</style>"
    "</head>"
    "<body>"
    "<div class='container'>"
    "<h2>ESP32 OTA Manager</h2>"
    "<p>IP: 192.168.5.1</p>"
    "<p>Rete: MyESPfamily</p>"
    "<form method='POST' action='/upload' enctype='multipart/form-data'>"
    "<input type='file' name='firmware' accept='.bin'>"
    "<br>"
    "<button type='submit'>Carica Firmware</button>"
    "</form>"
    "<div class='status'>"
    "<p>Premi D sul dispositivo per uscire (riavvio)</p>"
    "</div>"
    "</div>"
    "</body>"
    "</html>";

static esp_err_t ota_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, OTA_HTML, strlen(OTA_HTML));
    return ESP_OK;
}

static esp_err_t ota_upload_handler(httpd_req_t *req) {
    char buf[1024];
    int remaining = req->content_len;
    esp_ota_handle_t update_handle = 0;
    const esp_partition_t *update_part = NULL;

    ESP_LOGI(TAG, "Inizio upload firmware, size = %d", remaining);

    update_part = esp_ota_get_next_update_partition(NULL);
    if (update_part == NULL) {
        ESP_LOGE(TAG, "Nessuna partizione OTA disponibile");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA partition missing");
        return ESP_FAIL;
    }

    esp_err_t ret = esp_ota_begin(update_part, OTA_SIZE_UNKNOWN, &update_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin fallita: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    while (remaining > 0) {
        int to_read = (remaining > sizeof(buf)) ? sizeof(buf) : remaining;
        int received = httpd_req_recv(req, buf, to_read);
        if (received <= 0) {
            ESP_LOGE(TAG, "Errore ricezione chunk");
            esp_ota_abort(update_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Data reception error");
            return ESP_FAIL;
        }
        ret = esp_ota_write(update_handle, buf, received);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write fallita: %s", esp_err_to_name(ret));
            esp_ota_abort(update_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            return ESP_FAIL;
        }
        remaining -= received;
    }

    ret = esp_ota_end(update_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end fallita: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
        return ESP_FAIL;
    }

    ret = esp_ota_set_boot_partition(update_part);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition fallita: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Firmware ricevuto correttamente. Riavvio...");
    httpd_resp_send(req, "Firmware upload completato. Riavvio in corso...", -1);

    tft_fill_screen(TFT_BLACK);
    tft_set_text_color(TFT_GREEN);
    tft_set_text_size(2);
    tft_set_cursor(20, 140);
    tft_print("Aggiornamento completato!");
    tft_set_cursor(20, 180);
    tft_print("Riavvio in corso...");
    vTaskDelay(pdMS_TO_TICKS(2000));

    esp_restart();
    return ESP_OK;
}

static void start_http_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.stack_size = 8192;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_get = { .uri = "/", .method = HTTP_GET, .handler = ota_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_get);
        httpd_uri_t uri_post = { .uri = "/upload", .method = HTTP_POST, .handler = ota_upload_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_post);
        ESP_LOGI(TAG, "Server HTTP avviato");
    } else {
        ESP_LOGE(TAG, "Impossibile avviare server HTTP");
    }
}

// Inizializza WiFi in modalità AP senza chiamare esp_wifi_init
static void wifi_init_softap(void) {
    // Ottieni o crea l'interfaccia AP
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif == NULL) {
        ap_netif = esp_netif_create_default_wifi_ap();
    }

    // Configura l'AP (WiFi già inizializzato dal modulo ESP‑Now)
    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.ap.ssid, OTA_SSID);
    wifi_config.ap.ssid_len = strlen(OTA_SSID);
    strcpy((char*)wifi_config.ap.password, OTA_PASSWORD);
    wifi_config.ap.channel = OTA_CHANNEL;
    wifi_config.ap.authmode = (strlen(OTA_PASSWORD) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_WPA2_PSK;
    wifi_config.ap.ssid_hidden = 0;
    wifi_config.ap.max_connection = OTA_MAX_CONN;
    wifi_config.ap.beacon_interval = 100;
    wifi_config.ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    // Imposta IP statico per l'AP
    if (ap_netif) {
        esp_netif_ip_info_t ip_info;
        esp_netif_str_to_ip4(OTA_LOCAL_IP, &ip_info.ip);
        esp_netif_str_to_ip4(OTA_GATEWAY, &ip_info.gw);
        esp_netif_str_to_ip4(OTA_NETMASK, &ip_info.netmask);
        esp_netif_dhcps_stop(ap_netif);
        esp_netif_set_ip_info(ap_netif, &ip_info);
        esp_netif_dhcps_start(ap_netif);
    }

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Wi-Fi AP avviato. SSID: %s IP: %s", OTA_SSID, OTA_LOCAL_IP);
}

void openOTAManager(void) {
    // Ferma ESP‑Now e ferma WiFi (senza deinizializzare)
    esp_now_deinit();
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(100));

    // Riconfigura WiFi in modalità AP
    wifi_init_softap();

    // Avvia server HTTP
    start_http_server();

    // Schermata OTA
    tft_fill_screen(TFT_BLACK);
    tft_set_text_color(TFT_CYAN);
    tft_set_text_size(3);
    const char* title = " OTA MANAGER";
    int16_t tw = tft_text_width(title) * 3;
    int16_t x = (480 - tw) / 2;
    tft_set_cursor(x, 20);
    tft_print(title);
    tft_set_text_size(2);
    tft_set_text_color(TFT_YELLOW);
    tft_set_cursor(20, 80);
    tft_print("Upload firmware via Web");
    tft_set_cursor(20, 120);
    tft_printf("Network: %s", OTA_SSID);
    tft_set_cursor(20, 160);
    tft_printf("Password: 12345678");
    tft_set_cursor(20, 200);
    tft_printf("IP: %s", OTA_LOCAL_IP);
    tft_set_cursor(20, 260);
    tft_print("Press D for exit without loading");

    // Loop attesa uscita
    while (true) {
        char key = readKeypad();
        if (key == 'D') {
            ESP_LOGI(TAG, "Uscita da OTA Manager (senza upload) - Riavvio");
            if (server) {
                httpd_stop(server);
                server = NULL;
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}