#pragma once

#include "GcodeTypes.h"
#include <string>

// Setup mandrino
std::string generateSetupBlock(const SpindleParams& s, bool includeComments);

// Selezione tipo tornitura
std::string selectTurningType(const TurningParams& tp, std::string& gcodeType, bool includeComments);

// Generatori specifici
std::string generateCylindricalTurningGcode(const TurningParams& p, bool includeComments);
std::string generateExternalTaperGcode(const TurningParams& p, bool includeComments);
std::string generateInternalTaperGcode(const TurningParams& p, bool includeComments);
std::string generateFacingGcode(const FacingParams& p, bool includeComments);
std::string generatePartingGcode(const PartingParams& cp, bool includeComments);
std::string generateConcaveHemisphericalGcode(const HemisphericalParams& hp, bool includeComments);
std::string generateConvexHemisphericalGcode(const HemisphericalParams& hp, bool includeComments);
std::string generateChamferBilletGcode(const ChamferBilletParams& p, bool includeComments);
std::string generateThreadingGcode(const ThreadingParams& p, bool includeComments);
std::string generateKeywayGcode(const KeywayParams& kp, bool includeComments);
std::string generateEndBlock(bool includeComments);

// Costruttori di blocchi
GcodeBlock buildTurningBlock(const TurningParams& tp, bool includeComments);
GcodeBlock buildFacingBlock(const FacingParams& fp, bool includeComments);
GcodeBlock buildPartingBlock(const PartingParams& cp, bool includeComments);
GcodeBlock buildHemisphericalBlock(const HemisphericalParams& hp, bool includeComments);
GcodeBlock buildChamferBilletBlock(const ChamferBilletParams& cp, bool includeComments);
GcodeBlock buildThreadingBlock(const ThreadingParams& tp, bool includeComments);
GcodeBlock buildKeywayBlock(const KeywayParams& kp, bool includeComments);

// Funzioni di calcolo
float calculateRPMfromCSS(float css_m_min, float diametro_mm);
float calculateMaxCSS(float diametro_mm, int maxRPM);
float calculateTaperLength(float startDiam, float targetDiam, float angleDeg);
int calculateCSSrpm(float diametro, float mmin);

// Generatore principale
void GcodeGenerator(const GcodeBlock& block);