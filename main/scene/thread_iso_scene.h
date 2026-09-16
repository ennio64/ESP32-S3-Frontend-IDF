#ifndef THREAD_ISO_SCENE_H
#define THREAD_ISO_SCENE_H

#ifdef __cplusplus
extern "C" {
#endif

// Struttura pubblica per i passi normalizzati
struct ThreadPitchInfo {
    float diameter;         // Diametro nominale (mm)
    float coarse_pitch;     // Passo grosso (ISO 724)
    float fine_pitch1;      // Primo passo fine (ISO 262)
    float fine_pitch2;      // Secondo passo fine (0 se inesistente)
    float fine_pitch3;      // Terzo passo fine (0 se inesistente)
};

extern const ThreadPitchInfo threadStandardPitches[];
extern const int threadStandardPitchesCount;

// Tipi di standardizzazione
enum ThreadStandardType {
    THREAD_NON_STANDARD = 0,
    THREAD_STANDARD_COARSE,
    THREAD_STANDARD_FINE
};

// Funzioni parametriche (usate da threading_scene)
ThreadStandardType checkStandardThread(float dn, float pitch, bool isExternal);
bool getExternalLimits(float dn, float pitch,
                       float &dmax, float &dmin,
                       float &d2max, float &d2min,
                       float &d1max,
                       float &h, int &nap);
bool getInternalLimits(float dn, float pitch,
                       float &D, float &D2min, float &D2max,
                       float &D1min, float &D1max,
                       float &H, int &nap);

// Funzioni di compatibilità (per threading_scene)
int  getThreadMaxIndexExternal(void);
float getThreadPitchExternal(int index);
const char* getThreadDesignationExternal(int index);
int  getThreadMaxIndexInternal(void);
float getThreadPitchInternal(int index);
const char* getThreadDesignationInternal(int index);

// Scena di consultazione tabella
void thread_iso_scene_enter(void);

#ifdef __cplusplus
}
#endif

#endif // THREAD_ISO_SCENE_H