// grbl_commands.h
#ifndef GRBL_COMMANDS_H
#define GRBL_COMMANDS_H

// -------------------------------------------------------------------
// Comandi GRBL/HAL testuali
// -------------------------------------------------------------------
#define CMD_STATUS                "?"        // Stato macchina compatto
#define CMD_GET_INFO              "$I"       // Info firmware
#define CMD_GET_SETTINGS          "$$"       // Parametri macchina
#define CMD_GET_PARSER_STATE      "$G"       // Stato modale attivo
#define CMD_HOMING                "$H"       // Ciclo di homing
#define CMD_UNLOCK                "$X"       // Sblocca allarmi
#define CMD_GET_OFFSETS           "$#"       // Offset coordinate
#define CMD_GET_STARTUP_LINES     "$N"       // Comandi startup
#define CMD_GET_ERROR_MESSAGES    "$EE"      // Mappa codici errore
#define CMD_GET_ALARM_MESSAGES    "$EA"      // Mappa codici allarme
#define CMD_GET_COMPATIBILITY     "$C"       // Modalità check G‑code
#define CMD_GET_PROBE_STATE       "$P"       // Stato probe
#define CMD_GET_TOOL_TABLE        "$T"       // Tabella utensili

// -------------------------------------------------------------------
// Comandi real‑time grblHAL (binari, 1 byte)
// -------------------------------------------------------------------
#define CMD_RESET               "\x18"       // Soft reset (Ctrl-X)
#define CMD_CYCLE_START         "\x81"       // Avvia ciclo
#define CMD_FEED_HOLD           "\x82"       // Pausa ciclo
#define CMD_SAFETY_DOOR         "\x84"       // Safety door trigger
#define CMD_JOG_CANCEL          "\x85"       // Annulla jog attivo
#define CMD_FEED_OVR_RESET      "\x90"       // Reset override feedrate
#define CMD_FEED_OVR_PLUS       "\x91"       // Aumenta feedrate +10%
#define CMD_FEED_OVR_MINUS      "\x92"       // Diminuisci feedrate -10%
#define CMD_RAPID_OVR_RESET     "\x94"       // Reset override rapid
#define CMD_RAPID_OVR_MEDIUM    "\x95"       // Rapid override 50%
#define CMD_RAPID_OVR_LOW       "\x96"       // Rapid override 25%
#define CMD_SPINDLE_OVR_RESET   "\x99"       // Reset override spindle
#define CMD_SPINDLE_OVR_PLUS    "\x9A"       // Aumenta spindle +10%
#define CMD_SPINDLE_OVR_MINUS   "\x9B"       // Diminuisci spindle -10%
#define CMD_COOLANT_FLOOD_TOGGLE "\xA0"      // Toggle refrigerante flood
#define CMD_COOLANT_MIST_TOGGLE  "\xA1"      // Toggle refrigerante mist
#define CMD_STATUS_REPORT       "\x80"       // Stato compatto
#define CMD_STATUS_REPORT_FULL  "\x87"       // Stato esteso

// -------------------------------------------------------------------
// Comando multi‑byte
// -------------------------------------------------------------------
#define CMD_REBOOT              "\x1B\x14"   // ESC + Ctrl‑T → reboot

#endif // GRBL_COMMANDS_H