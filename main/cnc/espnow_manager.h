#ifndef ESPNOW_MANAGER_H
#define ESPNOW_MANAGER_H

#include "global_vars.h"
#include "GRBLParser.h"
#include "esp_now.h"
#include <cstdint>
#include <string>
#include <functional>
#include <vector>
#include <atomic>

class ESPNowManager {
public:
    using PositionCallback = std::function<void(const Position&)>;
    using StateChangeCallback = std::function<void(MachineState oldState, MachineState newState)>;
    using ErrorCallback = std::function<void(const std::string& error)>;

    ESPNowManager();
    ~ESPNowManager();

    bool begin();
    bool isReady() const { return paired.load() && bridgePeerAdded; }

    bool sendCommand(const std::string& cmd);
    bool sendGCodeStream(const std::string& gcode);
    bool sendRealtime(uint8_t cmd);

    void update();

    // Getters
    MachineState getMachineState() const { return currentState; }
    Position getWorkPosition() const { return workPosition; }
    float getWorkPositionX() const { return workPosition.x; }
    float getWorkPositionY() const { return workPosition.y; }
    float getWorkPositionZ() const { return workPosition.z; }
    float getWorkPositionA() const { return workPosition.a; }
    Position getMachinePosition() const { return machinePosition; }
    Position getWorkCoordinateOffset() const { return workCoordinateOffset; }
    float getSpindleSpeed() const { return spindleSpeed; }
    float getRealSpindleSpeed() const { return realSpindleSpeed; }
    std::string getSpindleDirection() const { return spindleDirection; }
    int getPlannerBuffer() const { return plannerBuffer; }
    int getSerialBuffer() const { return serialBuffer; }
    std::string getStatus() const;
    std::string getFullStatus() const { return lastParsedStatus.lastStatusLine; }
    const GRBLParser::ParsedStatus& getParsedStatus() const { return lastParsedStatus; }

    void jog(float x, float y, float z, float feedrate);
    void stopJog() { sendRealtime(0x85); }

    std::string queryReply(const std::string& cmd, uint32_t timeout_ms = 3000);
    void resetController() { sendRealtime(0x18); }
    void getBufferState(int& planner, int& serial) const { planner = plannerBuffer; serial = serialBuffer; }
    bool isConnected() const { return isReady(); }

    void onPosition(PositionCallback cb) { positionCallback = cb; }
    void onStateChange(StateChangeCallback cb) { stateChangeCallback = cb; }
    void onError(ErrorCallback cb) { errorCallback = cb; }

private:
    uint8_t broadcastMac[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    uint8_t bridgeMac[6] = {0};
    std::atomic<bool> paired{false};
    bool bridgePeerAdded = false;
    uint16_t sequence = 0;
    volatile uint16_t lastAckSeq = 0xFFFF;
    TickType_t lastDataTime = 0;
    TickType_t pairStartTime = 0;
    int currentChannel = 11;

    static constexpr int RX_QUEUE_SIZE = 64;
    static constexpr TickType_t HEARTBEAT_TIMEOUT_MS = 5000;    // 5 secondi
    static constexpr TickType_t PAIR_RETRY_INTERVAL_MS = 5000;  // 5 secondi

    std::atomic<unsigned long> packetCounter{0};
    unsigned long lastPacketCount = 0;
    TickType_t lastActivityTime = 0;

    struct RxPacket {
        uint8_t data[256];
        int len;
    };
    RxPacket rxQueue[RX_QUEUE_SIZE];
    volatile int rxHead = 0, rxTail = 0;
    volatile bool rxAvailable = false;

    std::string lineBuffer;
    std::string waitingForCmd;
    std::string pendingReply;

    MachineState currentState = STATE_UNKNOWN;
    Position machinePosition;
    Position workPosition;
    Position workCoordinateOffset;
    float spindleSpeed = 0, realSpindleSpeed = 0;
    std::string spindleDirection = "Unknown";
    int plannerBuffer = 0, serialBuffer = 0;
    GRBLParser::ParsedStatus lastParsedStatus;

    PositionCallback positionCallback = nullptr;
    StateChangeCallback stateChangeCallback = nullptr;
    ErrorCallback errorCallback = nullptr;

    bool scanChannelFromKnownNetworks();
    bool waitForPairing(uint32_t timeout_ms);
    bool sendWithAck(const uint8_t* data, size_t len, uint16_t seq);
    void processReceivedData(const uint8_t* data, int len);
    void processLine(const std::string& line);
    void updateFromParsedStatus(const GRBLParser::ParsedStatus& parsed);
    void resetPairing();

    static void onSend(const wifi_tx_info_t* tx_info, esp_now_send_status_t status);
    static void onRecv(const esp_now_recv_info_t* recv_info, const uint8_t* data, int len);
    static ESPNowManager* instance;
    static portMUX_TYPE spinlock;
};

#endif