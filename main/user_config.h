// user_config.h
#ifndef USER_CONFIG_H
#define USER_CONFIG_H

// Tipo di joystick:
// 0 = analogico (default, due assi su canali 2 e 3 dell'ADS1115)
// 1 = digitale (quattro tasti direzionali su canali 0-3)
#define JOYSTICK_TYPE_DEFAULT   1   // <-- modifica qui per cambiare default
// X MODE:
// 0 = diametro 
// 1 = raggio
#define X_MODE_DEFAULT   1   // <-- modifica qui per cambiare default

// Parametri per comando $37 di blocco/sblocco mandrino
#define SPINDLE_UNLOCK_PARAM_DEFAULT   5
#define SPINDLE_LOCK_PARAM_DEFAULT     13

// Valori di default per direzione spindle e encoder
// NOTA: spindle_dir e encoder_dir influiscono solo sulla visualizzazione del goniometro
// (verso di incremento dei gradi sul display). NON modificano il movimento reale
// dell'asse A o l'encoder fisico; serve solo per adattare la lettura a preferenze
// dell'operatore o a montaggi meccanici speculari.
#ifndef SPINDLE_DIR_DEFAULT
#define SPINDLE_DIR_DEFAULT   0   // 0 = senso orario, 1 = antiorario
#endif

#ifndef ENCODER_DIR_DEFAULT
#define ENCODER_DIR_DEFAULT   0
#endif

// Coefficienti di profondità filettatura (default 1.0)
// Questi coefficienti moltiplicano la profondità teorica K = 0.6134 * passo.
// Valori > 1.0 aumentano la profondità (filetto più profondo), < 1.0 la riducono.
// Per esterno (vite): aumentare K riduce il diametro di fondo (d1).
// Per interno (dado): aumentare K aumenta il diametro nominale (D).
#define EXT_DEPTH_COEFF_DEFAULT   1.0f
#define INT_DEPTH_COEFF_DEFAULT   1.0f

// ========== Parametri PROBE ==========
#ifndef PROBE_RING_OUTER_DIAMETER_DEFAULT
#define PROBE_RING_OUTER_DIAMETER_DEFAULT   24.5f   // diametro esterno anello (mm) - per probe STANDARD
#endif

#ifndef PROBE_RING_INNER_DIAMETER_DEFAULT
#define PROBE_RING_INNER_DIAMETER_DEFAULT   15.0f   // diametro interno anello (mm) - per probe INTERNAL
#endif

#ifndef PROBE_RING_THICKNESS_DEFAULT
#define PROBE_RING_THICKNESS_DEFAULT        9.5f    // spessore anello (mm)
#endif

#ifndef PROBE_FEED_DEFAULT
#define PROBE_FEED_DEFAULT                  50.0f   // velocità probe (mm/min)
#endif

#ifndef PROBE_SAFETY_DIST_DEFAULT
#define PROBE_SAFETY_DIST_DEFAULT           1.0f    // distanza di sicurezza (mm)
#endif

#endif