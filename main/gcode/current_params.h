// current_params.h
#ifndef CURRENT_PARAMS_H
#define CURRENT_PARAMS_H

#include "GcodeTypes.h"

// Dichiarazioni delle funzioni getter (implementate nelle rispettive scene)
TurningParams getCurrentTurningParams(void);
FacingParams getCurrentFacingParams(void);
PartingParams getCurrentPartingParams(void);
HemisphericalParams getCurrentHemisphericalParams(void);
ChamferBilletParams getCurrentChamferBilletParams(void);
ThreadingParams getCurrentThreadingParams(void);
KeywayParams getCurrentKeywayParams(void);

#endif