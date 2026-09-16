#include "espnow_manager.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/task.h"
#include <cstring>
#include <algorithm>

static const char *TAG = "ESPNOW";
ESPNowManager *ESPNowManager::instance = nullptr;
portMUX_TYPE ESPNowManager::spinlock = portMUX_INITIALIZER_UNLOCKED;

ESPNowManager::ESPNowManager() { instance = this; }
ESPNowManager::~ESPNowManager()
{
    if (instance == this)
        instance = nullptr;
}

// -------------------------------------------------------------------
bool ESPNowManager::scanChannelFromKnownNetworks()
{
    const char *knownNetworks[] = {"TISCALI-5311", "TISCALI-07EE7E", "HUAWEI P30 lite"};
    ESP_LOGI(TAG, "Scansione WiFi per canale...");

    wifi_scan_config_t scanConfig = {};
    scanConfig.ssid = nullptr;
    scanConfig.bssid = nullptr;
    scanConfig.channel = 0;
    scanConfig.show_hidden = false;
    scanConfig.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    scanConfig.scan_time.active.min = 100;
    scanConfig.scan_time.active.max = 300;
    scanConfig.home_chan_dwell_time = 0;
    scanConfig.channel_bitmap.ghz_2_channels = 0;
    scanConfig.channel_bitmap.ghz_5_channels = 0;
    scanConfig.coex_background_scan = false;

    if (esp_wifi_scan_start(&scanConfig, true) != ESP_OK)
        return false;
    uint16_t apNum = 0;
    esp_wifi_scan_get_ap_num(&apNum);
    if (apNum == 0)
        return false;
    wifi_ap_record_t *apList = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * apNum);
    esp_wifi_scan_get_ap_records(&apNum, apList);
    int bestChannel = -1, bestRSSI = -127;
    for (int i = 0; i < apNum; ++i)
    {
        std::string ssid((char *)apList[i].ssid);
        int rssi = apList[i].rssi;
        int channel = apList[i].primary;
        for (int j = 0; j < 3; ++j)
        {
            if (ssid == knownNetworks[j] && rssi > bestRSSI)
            {
                bestRSSI = rssi;
                bestChannel = channel;
                break;
            }
        }
    }
    free(apList);
    if (bestChannel > 0)
    {
        currentChannel = bestChannel;
        ESP_LOGI(TAG, "Canale %d (RSSI: %d)", currentChannel, bestRSSI);
        return true;
    }
    currentChannel = 11;
    return false;
}

// -------------------------------------------------------------------
bool ESPNowManager::begin()
{
    esp_netif_init();
    esp_event_loop_create_default();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    scanChannelFromKnownNetworks();
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    if (esp_now_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "ESP-NOW init fail");
        return false;
    }
    esp_now_register_send_cb(onSend);
    esp_now_register_recv_cb(onRecv);
    esp_now_peer_info_t broadcastPeer = {};
    memcpy(broadcastPeer.peer_addr, broadcastMac, 6);
    broadcastPeer.channel = currentChannel;
    broadcastPeer.encrypt = false;
    esp_now_add_peer(&broadcastPeer);
    ESP_LOGI(TAG, "Invio PAIR...");
    esp_now_send(broadcastMac, (const uint8_t *)"PAIR", 4);
    pairStartTime = xTaskGetTickCount();
    lastDataTime = pairStartTime;
    lastActivityTime = pairStartTime;
    return waitForPairing(10000);
}

bool ESPNowManager::waitForPairing(uint32_t timeout_ms)
{
    TickType_t start = xTaskGetTickCount();
    while (!paired.load() && (xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms))
    {
        vTaskDelay(pdMS_TO_TICKS(10));
        update();
    }
    if (paired.load())
    {
        ESP_LOGI(TAG, "Bridge accoppiato!");
        return true;
    }
    ESP_LOGE(TAG, "Pairing fallito");
    return false;
}

void ESPNowManager::resetPairing()
{
    if (paired.load())
        ESP_LOGI(TAG, "Reset pairing");
    paired.store(false);
    bridgePeerAdded = false;

    // Salva il MAC corrente prima di azzerarlo
    uint8_t oldMac[6];
    memcpy(oldMac, bridgeMac, 6);
    bool hasPeer = (oldMac[0] != 0 || oldMac[1] != 0 || oldMac[2] != 0 ||
                    oldMac[3] != 0 || oldMac[4] != 0 || oldMac[5] != 0);

    // Cancella il peer se esiste
    if (hasPeer) {
        esp_err_t err = esp_now_del_peer(oldMac);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_now_del_peer fallito: %d", err);
        }
    }

    // Ora azzera bridgeMac e altri contatori
    memset(bridgeMac, 0, 6);
    sequence = 0;
    lastAckSeq = 0xFFFF;

    // Invia PAIR in broadcast
    esp_now_send(broadcastMac, (const uint8_t *)"PAIR", 4);
    pairStartTime = xTaskGetTickCount();
    lastActivityTime = 0;
    lastPacketCount = 0;
}

// -------------------------------------------------------------------
bool ESPNowManager::sendWithAck(const uint8_t *data, size_t len, uint16_t seq)
{
    esp_err_t err = esp_now_send(bridgeMac, data, len);
    if (err != ESP_OK)
        return false;
    TickType_t start = xTaskGetTickCount();
    while (lastAckSeq != seq)
    {
        if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(3000))
            return false;
        vTaskDelay(pdMS_TO_TICKS(1));
        update();
    }
    return true;
}

bool ESPNowManager::sendCommand(const std::string &cmd)
{
    if (!paired.load() || !bridgePeerAdded)
    {
        if (errorCallback)
            errorCallback("Not paired");
        return false;
    }
    std::string command = cmd;
    bool isRealtime = (command.length() == 1 && (command[0] == '!' || command[0] == '~' || command[0] == 0x18));
    if (!isRealtime)
    {
        if (!command.empty() && command.back() != '\n')
            command += '\n';
        std::transform(command.begin(), command.end(), command.begin(), ::toupper);
    }
    size_t len = command.length();
    if (len > 246)
        len = 246;
    uint8_t packet[250];
    packet[0] = sequence & 0xFF;
    packet[1] = (sequence >> 8) & 0xFF;
    packet[2] = len & 0xFF;
    packet[3] = (len >> 8) & 0xFF;
    memcpy(&packet[4], command.c_str(), len);
    bool ok = sendWithAck(packet, len + 4, sequence);
    if (ok)
        sequence++;
    return ok;
}

bool ESPNowManager::sendGCodeStream(const std::string &gcode)
{
    if (!paired.load() || !bridgePeerAdded)
    {
        ESP_LOGW(TAG, "Attendi pairing...");
        return false;
    }
    std::vector<std::string> lines;
    std::string cur;
    for (char c : gcode)
    {
        if (c == '\n' || c == '\r')
        {
            if (!cur.empty())
                lines.push_back(cur + "\n");
            cur.clear();
        }
        else
            cur += c;
    }
    if (!cur.empty())
        lines.push_back(cur + "\n");
    ESP_LOGI(TAG, "Righe da inviare: %d", (int)lines.size());
    int ok = 0;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (sendCommand(lines[i]))
            ok++;
        else
            ESP_LOGE(TAG, "Errore riga %d", (int)i + 1);
    }
    ESP_LOGI(TAG, "Completato: %d/%d OK", ok, (int)lines.size());
    return ok == (int)lines.size();
}

bool ESPNowManager::sendRealtime(uint8_t cmd)
{
    if (!paired.load() || !bridgePeerAdded)
        return false;
    uint8_t packet[5] = {(uint8_t)(sequence & 0xFF), (uint8_t)((sequence >> 8) & 0xFF), 1, 0, cmd};
    bool ok = sendWithAck(packet, 5, sequence);
    if (ok)
        sequence++;
    return ok;
}

// -------------------------------------------------------------------
void ESPNowManager::update()
{
    TickType_t now = xTaskGetTickCount();
    unsigned long currentCount = packetCounter.load();

    // Heartbeat passivo: rileva se i pacchetti sono fermi
    if (currentCount != lastPacketCount)
    {
        lastPacketCount = currentCount;
        lastActivityTime = now;
    }
    else
    {
        if (paired.load() && lastActivityTime != 0 &&
            (now - lastActivityTime) > pdMS_TO_TICKS(HEARTBEAT_TIMEOUT_MS))
        {
            ESP_LOGW(TAG, "Bridge lost (no packets for %lu ms), reset pairing", HEARTBEAT_TIMEOUT_MS);
            resetPairing();
            lastActivityTime = 0;
        }
    }

    // Ritentativi di pairing se non siamo accoppiati
    if (!paired.load())
    {
        if ((now - pairStartTime) > pdMS_TO_TICKS(PAIR_RETRY_INTERVAL_MS))
        {
            ESP_LOGI(TAG, "Retrying pairing...");
            if (bridgeMac[0] != 0)
                esp_now_del_peer(bridgeMac);
            memset(bridgeMac, 0, 6);
            esp_now_send(broadcastMac, (const uint8_t *)"PAIR", 4);
            pairStartTime = now;
        }
    }

    // Gestione coda RX
    if (rxAvailable)
    {
        portENTER_CRITICAL(&spinlock);
        int idx = rxTail;
        if (idx != rxHead)
        {
            RxPacket pkt = rxQueue[idx];
            rxTail = (idx + 1) % RX_QUEUE_SIZE;
            rxAvailable = (rxTail != rxHead);
            portEXIT_CRITICAL(&spinlock);
            processReceivedData(pkt.data, pkt.len);
        }
        else
        {
            portEXIT_CRITICAL(&spinlock);
        }
    }
}

void ESPNowManager::processReceivedData(const uint8_t *data, int len)
{
    for (int i = 0; i < len; ++i)
    {
        char c = (char)data[i];
        if (c == '\n')
        {
            if (!lineBuffer.empty())
            {
                processLine(lineBuffer);
                lineBuffer.clear();
            }
        }
        else if (c != '\r')
            lineBuffer += c;
    }
}

void ESPNowManager::processLine(const std::string &line)
{
    lastDataTime = xTaskGetTickCount();
    if (!waitingForCmd.empty())
    {
        if (line[0] == '<')
            return;
        if (line.find(waitingForCmd) != std::string::npos || line.find('=') != std::string::npos)
        {
            pendingReply = line;
            waitingForCmd.clear();
        }
        return;
    }
    if (line == "ok")
        return;
    if (line.rfind("error:", 0) == 0)
    {
        if (errorCallback)
            errorCallback(line);
        return;
    }
    if (line.rfind("[MSG:", 0) == 0)
    {
        ESP_LOGI(TAG, "%s", line.c_str());
        return;
    }
    if (!line.empty() && line[0] == '<')
    {
        ESP_LOGI(TAG, "Status: %s", line.c_str());
        GRBLParser::ParsedStatus parsed = GRBLParser::parseCompleteStatus(line);
        grblCurrentLineNumber = parsed.lineNumber;
        updateFromParsedStatus(parsed);
    }
}

void ESPNowManager::updateFromParsedStatus(const GRBLParser::ParsedStatus &parsed)
{
    MachineState old = currentState;
    currentState = parsed.state;
    machinePosition = parsed.machinePosition;

    if (parsed.wcoFound)
    {
        workCoordinateOffset = parsed.workCoordinateOffset;
    }
    workPosition.x = machinePosition.x - workCoordinateOffset.x;
    workPosition.y = machinePosition.y - workCoordinateOffset.y;
    workPosition.z = machinePosition.z - workCoordinateOffset.z;
    workPosition.a = machinePosition.a - workCoordinateOffset.a;

    spindleSpeed = parsed.spindleSpeed;
    realSpindleSpeed = parsed.realSpindleSpeed;
    spindleDirection = parsed.spindleDirection;
    plannerBuffer = parsed.plannerBuffer;
    serialBuffer = parsed.serialBuffer;
    lastParsedStatus = parsed;

    if (positionCallback)
        positionCallback(workPosition);
    if (stateChangeCallback && old != currentState)
        stateChangeCallback(old, currentState);
}

// -------------------------------------------------------------------
std::string ESPNowManager::getStatus() const
{
    switch (currentState)
    {
    case STATE_IDLE:   return "Idle";
    case STATE_RUN:    return "Run";
    case STATE_HOLD:   return "Hold";
    case STATE_JOG:    return "Jog";
    case STATE_ALARM:  return "Alarm";
    case STATE_CHECK:  return "Check";
    case STATE_HOME:   return "Home";
    case STATE_SLEEP:  return "Sleep";
    case STATE_TOOL:   return "Tool";
    default:           return "Unknown";
    }
}

void ESPNowManager::jog(float x, float y, float z, float feedrate)
{
    std::string cmd = "$J=G91";
    if (x != 0) cmd += " X" + std::to_string(x);
    if (y != 0) cmd += " Y" + std::to_string(y);
    if (z != 0) cmd += " Z" + std::to_string(z);
    cmd += " F" + std::to_string(feedrate);
    sendCommand(cmd);
}

std::string ESPNowManager::queryReply(const std::string &cmd, uint32_t timeout_ms)
{
    if (!paired.load() || !bridgePeerAdded)
        return "";
    waitingForCmd = cmd;
    pendingReply.clear();
    if (!sendCommand(cmd))
    {
        waitingForCmd.clear();
        return "";
    }
    TickType_t start = xTaskGetTickCount();
    while (!waitingForCmd.empty() && (xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms))
    {
        update();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    std::string reply = pendingReply;
    pendingReply.clear();
    waitingForCmd.clear();
    return reply;
}

// -------------------------------------------------------------------
// Callback statiche con firme corrette per IDF v6.0
void ESPNowManager::onSend(const wifi_tx_info_t *tx_info, esp_now_send_status_t status)
{
    // non utilizzato
}

void ESPNowManager::onRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    if (!instance)
        return;
    instance->lastDataTime = xTaskGetTickCount();
    instance->packetCounter++;   // incremento contatore atomico

    // 1) ACK dal bridge
    if (len == 3 && data[2] == 0x01)
    {
        uint16_t seq = data[0] | (data[1] << 8);
        instance->lastAckSeq = seq;
        return;
    }

    // 2) Pairing con il bridge CNC
    if (len == 7 && memcmp(data, "PAIR_OK", 7) == 0)
    {
        memcpy(instance->bridgeMac, recv_info->src_addr, 6);
        esp_now_peer_info_t bridgePeer = {};
        memcpy(bridgePeer.peer_addr, instance->bridgeMac, 6);
        bridgePeer.channel = instance->currentChannel;
        bridgePeer.encrypt = false;
        if (esp_now_add_peer(&bridgePeer) == ESP_OK)
        {
            instance->bridgePeerAdded = true;
            instance->paired.store(true);
            ESP_LOGI(TAG, "✅ BRIDGE TROVATO! MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                     instance->bridgeMac[0], instance->bridgeMac[1], instance->bridgeMac[2],
                     instance->bridgeMac[3], instance->bridgeMac[4], instance->bridgeMac[5]);
        }
        return;
    }

    // 3) Gestione messaggio freno dal pendant
    if (len >= 7 && memcmp(data, "BRAKE:", 6) == 0)
    {
        static bool pendantPeerAdded = false;
        if (!pendantPeerAdded)
        {
            esp_now_peer_info_t pendantPeer = {};
            memcpy(pendantPeer.peer_addr, recv_info->src_addr, 6);
            pendantPeer.channel = instance->currentChannel;
            pendantPeer.encrypt = false;
            if (esp_now_add_peer(&pendantPeer) == ESP_OK)
            {
                pendantPeerAdded = true;
                ESP_LOGI(TAG, "✅ Pendant aggiunto come peer per ACK");
            }
            else
            {
                ESP_LOGE(TAG, "❌ Impossibile aggiungere peer pendant");
            }
        }

        extern bool brakeActive;
        bool newState = (len > 6 && data[6] == '1');
        brakeActive = newState;

        ESP_LOGI(TAG, "🔧 Stato freno aggiornato: %s",
                 brakeActive ? "ATTIVO (bloccato)" : "DISATTIVO (libero)");

        const char *ack = "BRAKE_ACK";
        esp_now_send(recv_info->src_addr, (const uint8_t *)ack, strlen(ack));
        return;
    }

    // 4) Gestione Ping Pong con bridge
    if (len == 4 && memcmp(data, "PING", 4) == 0)
    {
        esp_now_send(recv_info->src_addr, (const uint8_t *)"PONG", 4);
        return;
    }

    // 5) Dati normali (G-code, risposte, etc.)
    int nextHead = (instance->rxHead + 1) % RX_QUEUE_SIZE;
    if (nextHead != instance->rxTail)
    {
        RxPacket &pkt = instance->rxQueue[instance->rxHead];
        pkt.len = (len < 256) ? len : 256;
        memcpy(pkt.data, data, pkt.len);
        instance->rxHead = nextHead;
        instance->rxAvailable = true;
    }
    else
    {
        ESP_LOGW(TAG, "RX queue full");
    }
}