#ifndef BRAKE_ALERT_H
#define BRAKE_ALERT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Legge lo stato attuale del freno
bool isBrakeActive(void);

// Mostra alert a schermo intero
// Restituisce true se freno rilasciato, false se premuto D
bool showBrakeAlertAndWait(void);

// Verifica freno prima di entrare in una lavorazione
// Restituisce true se si può entrare, false se bloccato
bool checkBrakeBeforeMachining(void);

#ifdef __cplusplus
}
#endif

#endif