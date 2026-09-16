#ifndef GRBL_PARSER_H
#define GRBL_PARSER_H

#include <string>
#include <algorithm>
#include <cstdlib>   // per strtof
#include "global_vars.h" 
#include <cstring>   // per strlen

class GRBLParser {
public:
    struct ParsedStatus {
        MachineState state = STATE_UNKNOWN;
        Position machinePosition;
        Position workPosition;
        Position workCoordinateOffset;
        bool wcoFound = false;
        float feedrate = 0;
        float spindleSpeed = 0;
        float realSpindleSpeed = 0;
        std::string spindleDirection;
        int plannerBuffer = 0;
        int serialBuffer = 0;
        float dtgX = -999, dtgY = -999, dtgZ = -999;
        std::string pinState;
        std::string motionCommand;
        int lineNumber = -1;
        bool alarm = false, running = false, idle = false, homing = false;
        std::string lastStatusLine;
    };

    static ParsedStatus parseCompleteStatus(const std::string& status) {
        ParsedStatus result;
        if (status.empty() || status[0] != '<') return result;
        result.lastStatusLine = status;

        result.state = parseMachineStateEnum(status);
        parsePosition(status, result.machinePosition, true);      // MPos
        parsePosition(status, result.workCoordinateOffset, false); // WCO
        result.wcoFound = parsePosition(status, result.workCoordinateOffset, false);
        result.workPosition = result.machinePosition - result.workCoordinateOffset;
        result.feedrate = parseFeedrate(status);
        result.spindleSpeed = parseSpindleSpeed(status);
        result.realSpindleSpeed = parseRealSpindleSpeed(status);
        result.spindleDirection = parseSpindleDirectionFromStatus(status);
        parseBufferState(status, result.plannerBuffer, result.serialBuffer);
        result.lineNumber = parseLineNumber(status);
        parseDTG(status, result.dtgX, result.dtgY, result.dtgZ);
        result.pinState = parsePinState(status);
        result.motionCommand = parseMotionCommand(status);
        result.alarm = isAlarmState(status);
        result.running = isRunningState(status);
        result.idle = isIdleState(status);
        result.homing = isHomingState(status);
        return result;
    }

    static std::string parseMotionCommand(const std::string& status) {
        size_t cmd = status.find("Cmd:");
        if (cmd == std::string::npos) return "Unknown";
        size_t end = status.find('|', cmd);
        if (end == std::string::npos) end = status.find('>', cmd);
        if (end == std::string::npos) return "Unknown";
        std::string command = status.substr(cmd + 4, end - cmd - 4);
        size_t space = command.find(' ');
        if (space != std::string::npos) command = command.substr(0, space);
        size_t dot = command.find('.');
        if (dot != std::string::npos) command = command.substr(0, dot);
        return command;
    }

private:
    static std::string parseMachineState(const std::string& status) {
        size_t end = status.find('|');
        if (end == std::string::npos) return "Unknown";
        std::string state = status.substr(1, end - 1);
        size_t colon = state.find(':');
        if (colon != std::string::npos) state = state.substr(0, colon);
        return state;
    }

    static MachineState parseMachineStateEnum(const std::string& status) {
        std::string s = parseMachineState(status);
        if (s == "Idle") return STATE_IDLE;
        if (s == "Run") return STATE_RUN;
        if (s == "Hold") return STATE_HOLD;
        if (s == "Jog") return STATE_JOG;
        if (s == "Alarm") return STATE_ALARM;
        if (s == "Check") return STATE_CHECK;
        if (s == "Home") return STATE_HOME;
        if (s == "Sleep") return STATE_SLEEP;
        if (s == "Tool") return STATE_TOOL;
        return STATE_UNKNOWN;
    }

    static bool parseCoordinatesSimple(const std::string& coordStr, Position& pos) {
        size_t comma1 = coordStr.find(',');
        size_t comma2 = coordStr.find(',', comma1 + 1);
        size_t comma3 = coordStr.find(',', comma2 + 1);
        if (comma1 == std::string::npos || comma2 == std::string::npos) return false;

        char* endptr;
        std::string tmp;
        tmp = coordStr.substr(0, comma1);
        pos.x = strtof(tmp.c_str(), &endptr);
        if (endptr == tmp.c_str()) return false;

        tmp = coordStr.substr(comma1 + 1, comma2 - comma1 - 1);
        pos.y = strtof(tmp.c_str(), &endptr);
        if (endptr == tmp.c_str()) return false;

        tmp = coordStr.substr(comma2 + 1, comma3 - comma2 - 1);
        pos.z = strtof(tmp.c_str(), &endptr);
        if (endptr == tmp.c_str()) return false;

        if (comma3 != std::string::npos) {
            tmp = coordStr.substr(comma3 + 1);
            pos.a = strtof(tmp.c_str(), &endptr);
            if (endptr == tmp.c_str()) pos.a = 0;
        } else {
            pos.a = 0;
        }
        return true;
    }

    static bool parsePosition(const std::string& status, Position& pos, bool isMachine) {
        const char* tag = isMachine ? "MPos:" : "WCO:";
        size_t start = status.find(tag);
        if (start == std::string::npos) return false;
        start += strlen(tag);
        size_t end = status.find('|', start);
        if (end == std::string::npos) end = status.find('>', start);
        if (end == std::string::npos) return false;
        std::string coordStr = status.substr(start, end - start);
        return parseCoordinatesSimple(coordStr, pos);
    }

    static float parseFeedrate(const std::string& status) {
        size_t fs = status.find("FS:");
        if (fs == std::string::npos) return 0;
        size_t comma = status.find(',', fs);
        if (comma == std::string::npos) return 0;
        std::string val = status.substr(fs + 3, comma - fs - 3);
        return strtof(val.c_str(), nullptr);
    }

    static float parseSpindleSpeed(const std::string& status) {
        size_t fs = status.find("FS:");
        if (fs == std::string::npos) return 0;
        size_t comma1 = status.find(',', fs);
        size_t comma2 = status.find(',', comma1 + 1);
        if (comma1 == std::string::npos || comma2 == std::string::npos) return 0;
        size_t end = status.find('|', comma2);
        if (end == std::string::npos) end = status.find('>', comma2);
        std::string val = status.substr(comma1 + 1, comma2 - comma1 - 1);
        return strtof(val.c_str(), nullptr);
    }

    static float parseRealSpindleSpeed(const std::string& status) {
        size_t fs = status.find("FS:");
        if (fs == std::string::npos) return 0;
        size_t comma1 = status.find(',', fs);
        size_t comma2 = status.find(',', comma1 + 1);
        if (comma1 == std::string::npos || comma2 == std::string::npos) return 0;
        size_t end = status.find('|', comma2);
        if (end == std::string::npos) end = status.find('>', comma2);
        std::string val = status.substr(comma2 + 1, end - comma2 - 1);
        return strtof(val.c_str(), nullptr);
    }

    static std::string parseSpindleDirectionFromStatus(const std::string& status) {
        if (status.find("M3") != std::string::npos) return "CW";
        if (status.find("M4") != std::string::npos) return "CCW";
        if (status.find("M5") != std::string::npos) return "OFF";
        float sp = parseSpindleSpeed(status);
        return (sp > 0) ? "CW" : "OFF";
    }

    static bool parseBufferState(const std::string& status, int& planner, int& serial) {
        size_t bf = status.find("Bf:");
        if (bf == std::string::npos) return false;
        size_t comma = status.find(',', bf);
        size_t end = status.find('|', bf);
        if (end == std::string::npos) end = status.find('>', bf);
        if (comma == std::string::npos || end == std::string::npos) return false;
        std::string pStr = status.substr(bf + 3, comma - bf - 3);
        std::string sStr = status.substr(comma + 1, end - comma - 1);
        planner = std::stoi(pStr);
        serial = std::stoi(sStr);
        return true;
    }

    static int parseLineNumber(const std::string& status) {
        size_t ln = status.find("Ln:");
        if (ln == std::string::npos) return -1;
        size_t end = status.find('|', ln);
        if (end == std::string::npos) end = status.find('>', ln);
        if (end == std::string::npos) return -1;
        std::string val = status.substr(ln + 3, end - ln - 3);
        return std::stoi(val);
    }

    static bool parseDTG(const std::string& status, float& x, float& y, float& z) {
        size_t dtg = status.find("DTG:");
        if (dtg == std::string::npos) return false;
        size_t end = status.find('|', dtg);
        if (end == std::string::npos) end = status.find('>', dtg);
        if (end == std::string::npos) return false;
        std::string block = status.substr(dtg + 4, end - dtg - 4);
        size_t comma1 = block.find(',');
        size_t comma2 = block.find(',', comma1 + 1);
        if (comma1 == std::string::npos || comma2 == std::string::npos) return false;
        x = strtof(block.substr(0, comma1).c_str(), nullptr);
        y = strtof(block.substr(comma1 + 1, comma2 - comma1 - 1).c_str(), nullptr);
        z = strtof(block.substr(comma2 + 1).c_str(), nullptr);
        return true;
    }

    static std::string parsePinState(const std::string& status) {
        size_t pn = status.find("Pn:");
        if (pn == std::string::npos) return "";
        size_t end = status.find('|', pn);
        if (end == std::string::npos) end = status.find('>', pn);
        if (end == std::string::npos) return "";
        return status.substr(pn + 3, end - pn - 3);
    }

    static bool isAlarmState(const std::string& status) { return parseMachineState(status) == "Alarm"; }
    static bool isRunningState(const std::string& status) { return parseMachineState(status) == "Run"; }
    static bool isIdleState(const std::string& status) { return parseMachineState(status) == "Idle"; }
    static bool isHomingState(const std::string& status) { return parseMachineState(status) == "Home"; }
};

#endif