#pragma once

#include <cstdint>
#include <string>

// -------------------------------------------------------------------
// Strutture per i parametri delle lavorazioni – TUTTI I VALORI IN RAGGIO (tranne lunghezze Z lineari)
// -------------------------------------------------------------------

struct FacingParams {
    float startZ;               // Z iniziale (DRO)
    float targetZ;              // Z finale
    float clearanceX;           // ritrazione radiale di sicurezza (raggio)
    float clearanceZ;           // ritrazione assiale di sicurezza
    float startRadius;          // raggio iniziale (DRO_X)
    float endRadius;            // raggio finale
    int addProfileType;         // 0=none, 1=chamfer, -1=billet
    float addProfileDistance;   // distanza del profilo aggiuntivo (in Z)
    float passDepth;            // profondità di passata (raggio)
    float lastPassDepth;        // ultima passata (raggio)
    float feedRate;             // avanzamento normale
    float lastFeedRate;         // avanzamento finale
    int springPasses;           // numero di passate a vuoto
    bool useCSS;                // true = CSS, false = RPM
    float spindleMmin;          // velocità superficiale (m/min) per CSS
    float spindleRPM;           // giri/minuto per modalità RPM
    bool spindleCW;             // true = CW, false = CCW
};

struct TurningParams {
    float actualRadius;         // raggio attuale (DRO_X)
    float targetRadius;         // raggio finale
    float clearanceX;           // raggio di sicurezza
    float clearanceZ;           // distanza assiale di sicurezza
    float FeedRate;             // avanzamento normale
    float lastFeedRate;         // avanzamento finale
    float passDepth;            // profondità di passata (raggio)
    float lastPassDepth;        // ultima passata (raggio)
    float Length;               // lunghezza da tornire (in Z)
    int springPasses;           // passate a vuoto
    float startZ;               // Z iniziale (DRO)
    float angle;                // angolo di conicità (gradi, 0 = cilindrico)
    bool useCSS;                // true = CSS, false = RPM
    bool spindleCW;             // direzione mandrino
    float spindleRPM;           // RPM
    float spindleMmin;          // m/min per CSS
};

struct PartingParams {
    float startZ;               // Z iniziale (DRO)
    float startX;               // raggio di partenza (DRO_X)
    float retractX;             // ritrazione di sicurezza in X (raggio)
    float depth;                // profondità di taglio (raggio, positiva verso centro)
    float feedRate;             // avanzamento
    bool pulseEnable;           // taglio pulsato
    float pulseAdvance;         // avanzamento per impulso (mm)
    float pulsePause;           // pausa tra impulsi (secondi)
    bool useCSS;                // true = CSS
    float spindleMmin;          // m/min per CSS
    float spindleRPM;           // RPM
    bool spindleCW;             // direzione mandrino
};

struct HemisphericalParams {
    float startZ;               // Z iniziale
    float startX;               // raggio iniziale
    float clearance;            // distanza di sicurezza (raggio e Z)
    float actualRadius;         // raggio del pezzo
    bool profileType;           // true = concavo, false = convesso
    float radius;               // raggio della semisfera
    float passDepth;            // profondità di passata (raggio)
    float feedRate;             // avanzamento normale
    float lastPassDepth;        // ultima passata (raggio)
    float lastFeedRate;         // avanzamento finale
    int springPasses;           // passate a vuoto
    bool useCSS;
    float spindleMmin;
    float spindleRPM;
    bool spindleCW;
};

struct ChamferBilletParams {
    float startZ;
    float startX;               // raggio iniziale
    float clearance;
    float actualRadius;         // raggio del pezzo
    int type;                   // 1 = Chamfer, 2 = Billet, 0 = None
    float distanceZ;            // lunghezza del profilo in Z
    float distanceX;            // larghezza del profilo in X (raggio)
    float angle;                // angolo (solo per Billet)
    float passDepth;
    float feedRate;
    float lastPassDepth;
    float lastFeedRate;
    int springPasses;
    bool useCSS;
    float spindleMmin;
    float spindleRPM;
    bool spindleCW;
};

struct ThreadingParams {
    float startZ;
    float actualRadius;         // raggio esterno o interno
    bool threadType;            // true = esterno, false = interno
    bool rightHand;             // true = destrorso, false = sinistrorso
    float pitch;                // passo
    float length;               // lunghezza filettata
    float threadDepth;          // profondità del filetto (raggio)
    float firstCut;             // prima passata (J)
    float offset;               // offset radiale (I)
    int springPasses;           // passate a vuoto (H)
    float R;                    // conicità (opzionale)
    int L;                      // avanzamento notturno (opzionale)
    float E;                    // distanza finale (opzionale)
    float spindleRPM;
    bool spindleCW;
};

struct KeywayParams {
    float startZ;
    float startX;               // raggio
    float length;               // lunghezza in Z (Q)
    float depth;                // profondità in X (raggio) (D)
    float step;                 // avanzamento per passata (P)
    float toolWidth;            // larghezza utensile (S)
    float retract;              // ritrazione (R)
    float feed;                 // avanzamento (F)
    int repetitions;            // ripetizioni (L)
    bool returnToStart;         // ritorno a inizio (H)
    bool multiple;              // multiple keyway
    int count;                  // numero di chiavette
    float angleStep;            // angolo tra chiavette (gradi)
};

struct SpindleParams {
    bool useCSS;
    bool spindleCW;
    float spindleRPM;
    float spindleMmin;
    float radius;               // raggio corrente per il calcolo CSS
};

struct GcodeBlock {
    std::string setup;
    std::string motion;
    std::string end;
    std::string type;
};