#ifndef MSGBOX_H
#define MSGBOX_H

#include <string>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Mostra una MessageBox gialla con due opzioni.
 * @param title   Titolo (opzionale, può essere vuoto)
 * @param message Testo principale (può contenere '\n' per a capo)
 * @param btn1    Etichetta primo pulsante
 * @param key1    Tasto corrispondente (es. 'B')
 * @param btn2    Etichetta secondo pulsante
 * @param key2    Tasto corrispondente (es. 'C')
 * @param boxW    Larghezza finestra (default 280)
 * @param boxH    Altezza finestra (default 120)
 * @return true se premuto key1, false se premuto key2
 */
bool showYesNoBox(const std::string& title, const std::string& message,
                  const std::string& btn1, char key1,
                  const std::string& btn2, char key2,
                  int boxW = 280, int boxH = 120);

/**
 * @brief Wrapper per richiedere l'abilitazione del refrigerante.
 * @return true se l'utente sceglie Sì (B), false se No (C)
 */
inline bool askCoolant() {
    return showYesNoBox("Coolant", "Enable coolant for this operation?",
                        "YES", 'B', "NO", 'C');
}

/**
 * @brief Mostra un messaggio rosso "Scene not implemented" e attende 3 secondi.
 * @param featureName Nome della funzionalità non implementata
 */
void showNotImplementedMessage(const char *featureName);

#ifdef __cplusplus
}
#endif

#endif // MSGBOX_H