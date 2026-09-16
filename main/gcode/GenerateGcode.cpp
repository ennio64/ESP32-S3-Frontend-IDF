// GenerateGcode.cpp – versione per IDF, lavora in raggi, invia G-code alla coda
#include "GenerateGcode.h"
#include "global_settings.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "graphics_helpers.h"
#include "GcodeMonitor.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <cstring>

static const char* TAG = "GCODE_GEN";

extern bool coolantEnabled;
extern float maxSpindleRPM;
extern QueueHandle_t gcodeQueue;
extern float DRO_X, DRO_Z;

static std::string fmtFloat(float value, int decimals) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.*f", decimals, value);
    return std::string(buf);
}

// -------------------------------------------------------------------
// Setup mandrino (usa raggio)
// -------------------------------------------------------------------
std::string generateSetupBlock(const SpindleParams &s, bool includeComments) {
    std::string gcode;
    if (includeComments) gcode += "(Initial setup)\n";
    
    // Piano XZ, modalità diametro/raggio (G7/G8), unità mm
    gcode += "G18 ";
    if (settings_get_xmode() == 0)
        gcode += "G7 ";   // Diametro mode
    else
        gcode += "G8 ";   // Raggio mode
    gcode += "G21\n";
    
    if (coolantEnabled) {
        if (includeComments) gcode += "(Coolant ON)\n";
        gcode += "M8\n";
    }
    
    if (s.useCSS) {
        float diam = 2.0f * s.radius;
        int rpm = calculateCSSrpm(diam, s.spindleMmin);
        if (rpm <= 0) rpm = 500;
        if (includeComments) gcode += "(CSS ON)\n";
        gcode += std::string(s.spindleCW ? "M3" : "M4") + " S" + std::to_string(rpm) + " G4 P1\n";
        int safeMax = (maxSpindleRPM > 0.0f) ? (int)maxSpindleRPM : 3000;
        gcode += "G50 S" + std::to_string(safeMax) + "\n";
        gcode += "G96 S" + std::to_string((int)s.spindleMmin) + "\n";
    } else {
        int safeRPM = (s.spindleRPM > 0) ? (int)s.spindleRPM : 500;
        gcode += std::string(s.spindleCW ? "M3" : "M4") + " S" + std::to_string(safeRPM) + " G4 P1\n";
        gcode += "G97\n";
    }
    
    return gcode;
}

// -------------------------------------------------------------------
// Selezione tipo tornitura (raggi)
// -------------------------------------------------------------------
std::string selectTurningType(const TurningParams &tp, std::string &gcodeType, bool includeComments) {
    if (tp.angle == 0.0) {
        gcodeType = (tp.actualRadius > tp.targetRadius) ? "ExternalCylindrical" : "InternalCylindrical";
        return generateCylindricalTurningGcode(tp, includeComments);
    }
    if (tp.actualRadius > tp.targetRadius) {
        gcodeType = "ExternalTaper";
        return generateExternalTaperGcode(tp, includeComments);
    }
    gcodeType = "InternalTaper";
    return generateInternalTaperGcode(tp, includeComments);
}

// -------------------------------------------------------------------
// Tornitura cilindrica (raggi)
// -------------------------------------------------------------------
std::string generateCylindricalTurningGcode(const TurningParams &p, bool includeComments) {
    std::string gcode;
    bool isInternal = (p.actualRadius < p.targetRadius);
    float clearanceRadius, retractZ;
    float lengthTarget = p.startZ - p.Length;

    // Calcolo clearance e posizione di retrazione in base al tipo
    if (isInternal) {
        // Per interno: l'utensile parte DENTRO il foro (raggio minore del diametro iniziale)
        clearanceRadius = p.actualRadius - p.clearanceX;   // es. 10.5 - 0.5 = 10.0
        retractZ = p.startZ + p.clearanceZ;
        if (includeComments) gcode += "(Positioning inside the bore)\n";
    } else {
        // Esterno: utensile parte fuori dal diametro iniziale
        clearanceRadius = p.actualRadius + p.clearanceX;
        retractZ = p.startZ + p.clearanceZ;
        if (includeComments) gcode += "(Positioning outside the part)\n";
    }

    gcode += "G0 X" + fmtFloat(clearanceRadius, 3) + "\n";
    gcode += "G0 Z" + fmtFloat(retractZ, 3) + "\n";

    // Calcolo profondità totale e numero di passate
    float totalDepth = fabs(p.actualRadius - p.targetRadius);
    int nPassate = (int)ceil((totalDepth - p.lastPassDepth) / p.passDepth);
    float correctedPassDepth = (totalDepth - p.lastPassDepth) / nPassate;

    float currentRadius = p.actualRadius;
    for (int pass = 1; pass <= nPassate; ++pass) {
        // Per interno: aumenta raggio; per esterno: diminuisce
        currentRadius += isInternal ? correctedPassDepth : -correctedPassDepth;
        if (includeComments) {
            gcode += "(Pass: " + std::to_string(pass) + ", X=" + fmtFloat(currentRadius, 3) + ")\n";
        }
        // Avvicinamento radiale
        gcode += "G1 X" + fmtFloat(currentRadius, 3) + " F" + fmtFloat(p.FeedRate, 3) + "\n";
        // Taglio longitudinale
        gcode += "G1 Z" + fmtFloat(lengthTarget, 3) + " F" + fmtFloat(p.FeedRate, 3) + "\n";
        // Retrazione radiale (diversa per interno/esterno)
        if (isInternal) {
            // Per interno: rientra verso l'interno (raggio minore)
            gcode += "G0 X" + fmtFloat(clearanceRadius, 3) + "\n";
        } else {
            // Per esterno: si allontana ulteriormente
            gcode += "G0 X" + fmtFloat(currentRadius + p.clearanceX, 3) + "\n";
        }
        // Retrazione longitudinale
        gcode += "G0 Z" + fmtFloat(retractZ, 3) + "\n";
    }

    // --- Passata finale (finitura) ---
    float finalPassRadius = p.targetRadius;
    if (includeComments) gcode += "(Final pass)\n";
    gcode += "G1 X" + fmtFloat(finalPassRadius, 3) + " F" + fmtFloat(p.lastFeedRate, 0) + "\n";
    gcode += "G1 Z" + fmtFloat(lengthTarget, 3) + " F" + fmtFloat(p.lastFeedRate, 0) + "\n";
    if (isInternal) {
        gcode += "G0 X" + fmtFloat(clearanceRadius, 3) + "\n";
    } else {
        gcode += "G0 X" + fmtFloat(finalPassRadius + p.clearanceX, 3) + "\n";
    }
    gcode += "G0 Z" + fmtFloat(retractZ, 3) + "\n";

    // --- Spring pass ---
    for (int i = 0; i < p.springPasses; ++i) {
        if (includeComments) gcode += "(Spring pass " + std::to_string(i+1) + ")\n";
        gcode += "G1 X" + fmtFloat(finalPassRadius, 3) + " F" + fmtFloat(p.lastFeedRate, 0) + "\n";
        gcode += "G1 Z" + fmtFloat(lengthTarget, 3) + " F" + fmtFloat(p.lastFeedRate, 0) + "\n";
        if (isInternal) {
            gcode += "G0 X" + fmtFloat(clearanceRadius, 3) + "\n";
        } else {
            gcode += "G0 X" + fmtFloat(finalPassRadius + p.clearanceX, 3) + "\n";
        }
        gcode += "G0 Z" + fmtFloat(retractZ, 3) + "\n";
    }

    return gcode;
}

// -------------------------------------------------------------------
// Tornitura conica esterna (raggi)
// -------------------------------------------------------------------
std::string generateExternalTaperGcode(const TurningParams &p, bool includeComments) {
    std::string gcode;
    float clearanceRadius = p.actualRadius + p.clearanceX;
    float retractZ = p.startZ + p.clearanceZ;

    if (includeComments) gcode += "(Positioning X e Z)\n";
    gcode += "G0 X" + fmtFloat(clearanceRadius, 3) + "\n";
    gcode += "G0 Z" + fmtFloat(retractZ, 3) + "\n";

    float taperRad = p.angle * M_PI / 180.0f;
    float totalDepth = p.actualRadius - p.targetRadius;
    int nPassate = (int)ceil((totalDepth - p.lastPassDepth) / p.passDepth);
    float correctedPassDepth = (totalDepth - p.lastPassDepth) / nPassate;
    float deltaZ = correctedPassDepth / tan(taperRad);
    float Zstep = p.startZ - deltaZ;
    float currentRadius = p.actualRadius - correctedPassDepth;

    for (int pass = 1; pass <= nPassate; ++pass) {
        if (includeComments) {
            gcode += "(Pass: " + std::to_string(pass) + ", DOC: " + fmtFloat(currentRadius, 3) + ")\n";
        }
        gcode += "G1 X" + fmtFloat(currentRadius, 3) + " F" + fmtFloat(p.FeedRate, 3) + "\n";
        gcode += "G1 X" + fmtFloat(p.actualRadius, 3) + " Z-" + fmtFloat(Zstep, 3) + " F" + fmtFloat(p.FeedRate, 3) + "\n";
        gcode += "G0 X" + fmtFloat(clearanceRadius, 3) + "\n";
        gcode += "G0 Z" + fmtFloat(retractZ, 3) + "\n";
        currentRadius -= correctedPassDepth;
        Zstep += deltaZ;
    }

    float Zend = p.startZ - (totalDepth / tan(taperRad));
    if (includeComments) {
        gcode += "(Final pass)\n";
    }
    gcode += "G1 X" + fmtFloat(p.targetRadius, 3) + " F" + fmtFloat(p.lastFeedRate, 0) + "\n";
    gcode += "G1 X" + fmtFloat(p.actualRadius, 3) + " Z-" + fmtFloat(Zend, 3) + " F" + fmtFloat(p.lastFeedRate, 0) + "\n";
    gcode += "G0 X" + fmtFloat(clearanceRadius, 3) + "\n";
    gcode += "G0 Z" + fmtFloat(retractZ, 3) + "\n";

    for (int i = 0; i < p.springPasses; ++i) {
        if (includeComments) {
            gcode += "(Spring pass " + std::to_string(i+1) + ")\n";
        }
        gcode += "G1 X" + fmtFloat(p.targetRadius, 3) + " F" + fmtFloat(p.lastFeedRate, 0) + "\n";
        gcode += "G1 X" + fmtFloat(p.actualRadius, 3) + " Z-" + fmtFloat(Zend, 3) + " F" + fmtFloat(p.lastFeedRate, 0) + "\n";
        gcode += "G0 X" + fmtFloat(clearanceRadius, 3) + "\n";
        gcode += "G0 Z" + fmtFloat(retractZ, 3) + "\n";
    }
    return gcode;
}

// -------------------------------------------------------------------
// Tornitura conica interna (raggi)
// -------------------------------------------------------------------
std::string generateInternalTaperGcode(const TurningParams &p, bool includeComments) {
    std::string gcode;
    float clearanceRadius = p.targetRadius + p.clearanceX;
    float retractZ = p.startZ + p.clearanceZ;

    if (includeComments) gcode += "(Positioning X e Z)\n";
    gcode += "G0 X" + fmtFloat(clearanceRadius, 3) + "\n";
    gcode += "G0 Z" + fmtFloat(retractZ, 3) + "\n";

    float taperRad = p.angle * M_PI / 180.0f;
    float totalDepth = p.targetRadius - p.actualRadius;
    int nPassate = (int)ceil((totalDepth - p.lastPassDepth) / p.passDepth);
    float correctedPassDepth = (totalDepth - p.lastPassDepth) / nPassate;
    float deltaZ = correctedPassDepth / tan(taperRad);
    float Zstep = p.startZ - deltaZ;
    float currentRadius = p.actualRadius + correctedPassDepth;

    for (int pass = 1; pass <= nPassate; ++pass) {
        if (includeComments) {
            gcode += "(Pass: " + std::to_string(pass) + ", DOC: " + fmtFloat(currentRadius, 3) + ")\n";
        }
        gcode += "G1 X" + fmtFloat(currentRadius, 3) + " F" + fmtFloat(p.FeedRate, 3) + "\n";
        gcode += "G1 X" + fmtFloat(p.actualRadius, 3) + " Z-" + fmtFloat(Zstep, 3) + " F" + fmtFloat(p.FeedRate, 3) + "\n";
        gcode += "G0 X" + fmtFloat(clearanceRadius, 3) + "\n";
        gcode += "G0 Z" + fmtFloat(retractZ, 3) + "\n";
        currentRadius += correctedPassDepth;
        Zstep += deltaZ;
    }

    float Zend = p.startZ - (totalDepth / tan(taperRad));
    if (includeComments) {
        gcode += "(Final pass)\n";
    }
    gcode += "G1 X" + fmtFloat(p.targetRadius, 3) + " F" + fmtFloat(p.lastFeedRate, 0) + "\n";
    gcode += "G1 X" + fmtFloat(p.actualRadius, 3) + " Z-" + fmtFloat(Zend, 3) + " F" + fmtFloat(p.lastFeedRate, 0) + "\n";
    gcode += "G0 X" + fmtFloat(clearanceRadius, 3) + "\n";
    gcode += "G0 Z" + fmtFloat(retractZ, 3) + "\n";

    for (int i = 0; i < p.springPasses; ++i) {
        if (includeComments) {
            gcode += "(Spring pass " + std::to_string(i+1) + ")\n";
        }
        gcode += "G1 X" + fmtFloat(p.targetRadius, 3) + " F" + fmtFloat(p.lastFeedRate, 0) + "\n";
        gcode += "G1 X" + fmtFloat(p.actualRadius, 3) + " Z-" + fmtFloat(Zend, 3) + " F" + fmtFloat(p.lastFeedRate, 0) + "\n";
        gcode += "G0 X" + fmtFloat(clearanceRadius, 3) + "\n";
        gcode += "G0 Z" + fmtFloat(retractZ, 3) + "\n";
    }
    return gcode;
}

// -------------------------------------------------------------------
// Facing (spianatura) – raggi
// -------------------------------------------------------------------
std::string generateFacingGcode(const FacingParams &p, bool includeComments) {
    std::string gcode;
    if (p.startZ <= p.targetZ) {
        if (includeComments) gcode += "(ERRORE: startZ deve essere > targetZ)\n";
        return gcode;
    }

    float totalDepth = p.startZ - p.targetZ;
    int nPasses = 0;
    float correctedPassDepth = p.passDepth;
    if (totalDepth > p.lastPassDepth) {
        nPasses = (int)ceil((totalDepth - p.lastPassDepth) / p.passDepth);
        correctedPassDepth = (totalDepth - p.lastPassDepth) / nPasses;
    }

    float clearanceRadius = std::max(p.startRadius, p.endRadius) + p.clearanceX;
    float retractZ = p.startZ + p.clearanceZ;
    float currentZ = p.startZ;

    if (includeComments) gcode += "(Positioning X e Z)\n";
    gcode += "G0 Z" + fmtFloat(retractZ, 3) + "\n";
    gcode += "G0 X" + fmtFloat(clearanceRadius, 3) + "\n";

    for (int pass = 1; pass <= nPasses; ++pass) {
        currentZ -= correctedPassDepth;
        if (includeComments) {
            gcode += "(Pass: " + std::to_string(pass) + ", Z=" + fmtFloat(currentZ,3) + ")\n";
        }
        gcode += "G1 Z" + fmtFloat(currentZ, 3) + " F" + fmtFloat(p.feedRate, 0) + "\n";
        gcode += "G1 X" + fmtFloat(p.endRadius, 3) + " F" + fmtFloat(p.feedRate, 0) + "\n";
        gcode += "G0 Z" + fmtFloat(retractZ, 3) + "\n";
        gcode += "G0 X" + fmtFloat(clearanceRadius, 3) + "\n";
    }

    if (p.lastPassDepth > 0) {
        if (includeComments) gcode += "(Final Pass)\n";
        gcode += "G1 Z" + fmtFloat(p.targetZ, 3) + " F" + fmtFloat(p.lastFeedRate, 0) + "\n";
        gcode += "G1 X" + fmtFloat(p.endRadius, 3) + " F" + fmtFloat(p.lastFeedRate, 0) + "\n";
        gcode += "G0 Z" + fmtFloat(retractZ, 3) + "\n";
        gcode += "G0 X" + fmtFloat(clearanceRadius, 3) + "\n";
    }

    for (int i = 0; i < p.springPasses; ++i) {
        if (includeComments) gcode += "(Spring pass " + std::to_string(i+1) + ")\n";
        gcode += "G1 Z" + fmtFloat(p.targetZ, 3) + " F" + fmtFloat(p.lastFeedRate, 0) + "\n";
        gcode += "G1 X" + fmtFloat(p.endRadius, 3) + " F" + fmtFloat(p.lastFeedRate, 0) + "\n";
        gcode += "G0 Z" + fmtFloat(retractZ, 3) + "\n";
        gcode += "G0 X" + fmtFloat(clearanceRadius, 3) + "\n";
    }

    if (p.addProfileType != 0 && p.addProfileDistance > 0.0f) {
        float radiusBeforeProfile = p.startRadius;
        float profileAngleDeg = (p.addProfileType < 0) ? 30.0f : 45.0f;
        float profileTan = tan(profileAngleDeg * M_PI / 180.0f);
        float profileForSteps = p.addProfileDistance;
        bool needsFinalPass = false;
        if (p.addProfileDistance > p.lastPassDepth) {
            profileForSteps = p.addProfileDistance - p.lastPassDepth;
            needsFinalPass = true;
        }
        int profileSteps = std::max(1, (int)ceil(profileForSteps / p.passDepth));
        float step = profileForSteps / profileSteps;

        for (int i = 1; i <= profileSteps; ++i) {
            float offset = i * step;
            float targetZ = p.targetZ - offset;
            float startX = radiusBeforeProfile - offset * profileTan;
            if (includeComments) {
                std::string label = (p.addProfileType > 0) ? "Chamfer" : "Billet";
                gcode += "(" + label + " " + std::to_string(i) + "/" + std::to_string(profileSteps) + ": " + fmtFloat(step, 3) + "mm)\n";
            }
            gcode += "G1 X" + fmtFloat(startX, 3) + " Z" + fmtFloat(p.targetZ, 3) + " F" + fmtFloat(p.feedRate, 0) + "\n";
            gcode += "G1 X" + fmtFloat(radiusBeforeProfile, 3) + " Z" + fmtFloat(targetZ, 3) + " F" + fmtFloat(p.feedRate, 0) + "\n";
            gcode += "G0 Z" + fmtFloat(retractZ, 3) + "\n";
            gcode += "G0 X" + fmtFloat(clearanceRadius, 3) + "\n";
        }
        if (needsFinalPass) {
            float finalOffset = p.addProfileDistance;
            float finalTargetZ = p.targetZ - finalOffset;
            float finalStartX = radiusBeforeProfile - finalOffset * profileTan;
            if (includeComments) {
                std::string label = (p.addProfileType > 0) ? "Chamfer finale" : "Billet finale";
                gcode += "(" + label + ": " + fmtFloat(p.lastPassDepth, 3) + "mm)\n";
            }
            gcode += "G1 X" + fmtFloat(finalStartX, 3) + " Z" + fmtFloat(p.targetZ, 3) + " F" + fmtFloat(p.lastFeedRate, 0) + "\n";
            gcode += "G1 X" + fmtFloat(radiusBeforeProfile, 3) + " Z" + fmtFloat(finalTargetZ, 3) + " F" + fmtFloat(p.lastFeedRate, 0) + "\n";
            gcode += "G0 Z" + fmtFloat(retractZ, 3) + "\n";
            gcode += "G0 X" + fmtFloat(clearanceRadius, 3) + "\n";
        }
    }
    return gcode;
}

// -------------------------------------------------------------------
// Parting (taglio) – raggi
// -------------------------------------------------------------------
std::string generatePartingGcode(const PartingParams &cp, bool includeComments) {
    std::string gcode;
    if (cp.depth <= 0) return "(ERRORE: Depth deve essere > 0)\n";
    if (cp.feedRate <= 0) return "(ERRORE: Feed Rate deve essere > 0)\n";
    if (cp.pulseEnable && cp.pulseAdvance <= 0) return "(ERRORE: Advance deve essere > 0)\n";

    float startX = cp.startX;
    float finalX = cp.startX - cp.depth;

    if (includeComments) gcode += "(Positioning X)\n";
    gcode += "G0 X" + fmtFloat(startX, 3) + "\n";

    if (includeComments) {
        gcode += "(Parting operation)\n";
        if (cp.pulseEnable) {
            gcode += "(PulseCut adv=" + fmtFloat(cp.pulseAdvance, 3) + " ret=0.10 p=" + fmtFloat(cp.pulsePause, 3) + "s)\n";
        } else {
            gcode += "(Continuous cutting)\n";
        }
    }

    if (cp.pulseEnable) {
        float currentX = startX;
        float remaining = cp.depth;
        const float retract = 0.10f;
        int pulseIndex = 0;
        while (remaining > 0.001f) {
            pulseIndex++;
            float step = std::min(cp.pulseAdvance, remaining);
            currentX -= step;
            remaining -= step;
            if (includeComments) gcode += "(Pulse " + std::to_string(pulseIndex) + ": advance to X" + fmtFloat(currentX,3) + ")\n";
            gcode += "G1 X" + fmtFloat(currentX,3) + " F" + fmtFloat(cp.feedRate,3) + "\n";
            if (remaining > 0.001f) {
                if (includeComments) gcode += "(Retract + pause)\n";
                gcode += "G0 X" + fmtFloat(currentX + retract,3) + "\n";
                gcode += "G4 P" + fmtFloat(cp.pulsePause,3) + "\n";
            }
        }
    } else {
        gcode += "G1 X" + fmtFloat(finalX,3) + " F" + fmtFloat(cp.feedRate,3) + "\n";
    }

    float safeX = cp.startX + cp.retractX;
    if (includeComments) gcode += "(Retract to safe X)\n";
    gcode += "G0 X" + fmtFloat(safeX,3) + "\n";
    return gcode;
}

// -------------------------------------------------------------------
// Emisferico concavo (raggi)
// -------------------------------------------------------------------
std::string generateConcaveHemisphericalGcode(const HemisphericalParams &hp, bool includeComments) {
    std::string gcode;
    float Xc = DRO_X;
    float Zc = DRO_Z;
    float R = hp.radius;
    float clearance = hp.clearance;
    float retractX = Xc + clearance;
    float retractZ = Zc + clearance;

    if (includeComments) {
        gcode += "(CONCAVE HEMISPHERICAL)\n";
        gcode += "(Center X=" + fmtFloat(Xc,3) + " Z=" + fmtFloat(Zc,3) + ")\n";
        gcode += "(Radius=" + fmtFloat(R,3) + ")\n";
    }
    gcode += "G18\n";
    gcode += "G0 X" + fmtFloat(retractX,3) + "\n";
    gcode += "G0 Z" + fmtFloat(retractZ,3) + "\n";

    int nPass = (int)ceil((R - hp.lastPassDepth) / hp.passDepth);
    float stepR = (R - hp.lastPassDepth) / nPass;

    for (int p = 1; p <= nPass; ++p) {
        float r = p * stepR;
        float startX = Xc - r;
        float startZ = Zc;
        float endX = Xc;
        float endZ = Zc - r;
        if (includeComments) gcode += "(Rough pass " + std::to_string(p) + " R=" + fmtFloat(r,3) + ")\n";
        gcode += "G1 X" + fmtFloat(startX,3) + " Z" + fmtFloat(startZ,3) + " F" + fmtFloat(hp.feedRate,0) + "\n";
        gcode += "G3 X" + fmtFloat(endX,3) + " Z" + fmtFloat(endZ,3) + " R" + fmtFloat(r,3) + "\n";
        gcode += "G0 X" + fmtFloat(retractX,3) + "\n";
        gcode += "G0 Z" + fmtFloat(retractZ,3) + "\n";
    }

    if (hp.lastPassDepth > 0) {
        if (includeComments) gcode += "(Final pass R=" + fmtFloat(R,3) + ")\n";
        float startX = Xc - R;
        float startZ = Zc;
        float endX = Xc;
        float endZ = Zc - R;
        gcode += "G1 X" + fmtFloat(startX,3) + " Z" + fmtFloat(startZ,3) + " F" + fmtFloat(hp.lastFeedRate,0) + "\n";
        gcode += "G3 X" + fmtFloat(endX,3) + " Z" + fmtFloat(endZ,3) + " R" + fmtFloat(R,3) + "\n";
        gcode += "G0 X" + fmtFloat(retractX,3) + "\n";
        gcode += "G0 Z" + fmtFloat(retractZ,3) + "\n";
    }

    for (int i = 0; i < hp.springPasses; ++i) {
        if (includeComments) gcode += "(Spring pass " + std::to_string(i+1) + ")\n";
        float startX = Xc - R;
        float startZ = Zc;
        float endX = Xc;
        float endZ = Zc - R;
        gcode += "G1 X" + fmtFloat(startX,3) + " Z" + fmtFloat(startZ,3) + " F" + fmtFloat(hp.lastFeedRate,0) + "\n";
        gcode += "G3 X" + fmtFloat(endX,3) + " Z" + fmtFloat(endZ,3) + " R" + fmtFloat(R,3) + "\n";
        gcode += "G0 X" + fmtFloat(retractX,3) + "\n";
        gcode += "G0 Z" + fmtFloat(retractZ,3) + "\n";
    }
    gcode += "G0 X" + fmtFloat(Xc,3) + " Z" + fmtFloat(Zc,3) + "\n";
    return gcode;
}

// -------------------------------------------------------------------
// Emisferico convesso (raggi)
// -------------------------------------------------------------------
std::string generateConvexHemisphericalGcode(const HemisphericalParams &hp, bool includeComments) {
    std::string gcode;
    float X0 = DRO_X;
    float Z0 = DRO_Z;
    float R = hp.radius;
    float Xc = X0 - R;
    float Zc = Z0 - R;
    float clearance = hp.clearance;
    float retractX = X0 + clearance;
    float retractZ = Z0 + clearance;

    if (includeComments) {
        gcode += "(CONVEX HEMISPHERICAL)\n";
        gcode += "(Center X=" + fmtFloat(Xc,3) + " Z=" + fmtFloat(Zc,3) + ")\n";
        gcode += "(Radius=" + fmtFloat(R,3) + ")\n";
    }
    gcode += "G18\n";
    gcode += "G0 X" + fmtFloat(retractX,3) + "\n";
    gcode += "G0 Z" + fmtFloat(retractZ,3) + "\n";

    int nPass = (int)ceil((R - hp.lastPassDepth) / hp.passDepth);
    float stepR = (R - hp.lastPassDepth) / nPass;

    for (int p = 1; p <= nPass; ++p) {
        float r = p * stepR;
        float Xi = Xc;
        float Zi = Zc + r;
        float Xf = Xc + r;
        float Zf = Zc;
        if (includeComments) gcode += "(Rough pass " + std::to_string(p) + " R=" + fmtFloat(r,3) + ")\n";
        gcode += "G1 X" + fmtFloat(Xi,3) + " Z" + fmtFloat(Zi,3) + " F" + fmtFloat(hp.feedRate,0) + "\n";
        gcode += "G2 X" + fmtFloat(Xf,3) + " Z" + fmtFloat(Zf,3) + " R" + fmtFloat(r,3) + "\n";
        gcode += "G0 X" + fmtFloat(retractX,3) + "\n";
        gcode += "G0 Z" + fmtFloat(retractZ,3) + "\n";
    }

    if (hp.lastPassDepth > 0) {
        if (includeComments) gcode += "(Final pass R=" + fmtFloat(R,3) + ")\n";
        float Xi = Xc;
        float Zi = Zc + R;
        float Xf = Xc + R;
        float Zf = Zc;
        gcode += "G1 X" + fmtFloat(Xi,3) + " Z" + fmtFloat(Zi,3) + " F" + fmtFloat(hp.lastFeedRate,0) + "\n";
        gcode += "G2 X" + fmtFloat(Xf,3) + " Z" + fmtFloat(Zf,3) + " R" + fmtFloat(R,3) + "\n";
        gcode += "G0 X" + fmtFloat(retractX,3) + "\n";
        gcode += "G0 Z" + fmtFloat(retractZ,3) + "\n";
    }

    for (int i = 0; i < hp.springPasses; ++i) {
        if (includeComments) gcode += "(Spring pass " + std::to_string(i+1) + ")\n";
        float Xi = Xc;
        float Zi = Zc + R;
        float Xf = Xc + R;
        float Zf = Zc;
        gcode += "G1 X" + fmtFloat(Xi,3) + " Z" + fmtFloat(Zi,3) + " F" + fmtFloat(hp.lastFeedRate,0) + "\n";
        gcode += "G2 X" + fmtFloat(Xf,3) + " Z" + fmtFloat(Zf,3) + " R" + fmtFloat(R,3) + "\n";
        gcode += "G0 X" + fmtFloat(retractX,3) + "\n";
        gcode += "G0 Z" + fmtFloat(retractZ,3) + "\n";
    }
    gcode += "G0 X" + fmtFloat(X0,3) + " Z" + fmtFloat(Z0,3) + "\n";
    return gcode;
}

// -------------------------------------------------------------------
// Chamfer / Billet (raggi)
// -------------------------------------------------------------------
std::string generateChamferBilletGcode(const ChamferBilletParams &p, bool includeComments) {
    std::string gcode;
    float radiusStart = p.startX;
    float clearance = p.clearance;
    float retractZ = p.startZ + clearance;
    float retractX = radiusStart + clearance;

    bool isChamfer = (p.type == 1);
    float chamferAngle = isChamfer ? 45.0f : p.angle;
    float distanceZ = p.distanceZ;
    float distanceX = isChamfer ? p.distanceZ : p.distanceX;

    if (chamferAngle <= 0.0f || chamferAngle >= 90.0f) {
        if (includeComments) gcode += "(ERRORE: Angolo deve essere tra 0° e 90°)\n";
        return gcode;
    }

    float tanAngle = tan(chamferAngle * M_PI / 180.0f);
    if (isChamfer) {
        distanceX = distanceZ;
    } else if (fabs(tanAngle - (distanceX / distanceZ)) > 0.01f) {
        if (distanceZ > 0) distanceX = distanceZ * tanAngle;
        else if (distanceX > 0) distanceZ = distanceX / tanAngle;
    }

    if (includeComments) {
        if (isChamfer) gcode += "(CHAMFER 45° PROFILING)\n";
        else gcode += "(BILLET PROFILING - Angolo: " + fmtFloat(chamferAngle, 1) + "°)\n";
        gcode += "(Distance Z: " + fmtFloat(distanceZ, 3) + "mm)\n";
        gcode += "(Distance X: " + fmtFloat(distanceX, 3) + "mm)\n";
    }
    gcode += "G0 X" + fmtFloat(retractX, 3) + "\n";
    gcode += "G0 Z" + fmtFloat(retractZ, 3) + "\n";

    float totalDepthZ = distanceZ;
    int nPassate = 0;
    float correctedPassDepth = p.passDepth;
    if (totalDepthZ > p.lastPassDepth) {
        nPassate = (int)ceil((totalDepthZ - p.lastPassDepth) / p.passDepth);
        correctedPassDepth = (totalDepthZ - p.lastPassDepth) / nPassate;
    }
    if (includeComments && nPassate > 0) gcode += "(Rough passes: " + std::to_string(nPassate) + ")\n";

    for (int pass = 1; pass <= nPassate; ++pass) {
        float currentDepthZ = pass * correctedPassDepth;
        float currentDepthX = currentDepthZ * tanAngle;
        if (includeComments) {
            gcode += "(Pass: " + std::to_string(pass) + ", Z: " + fmtFloat(currentDepthZ, 3) + "mm, X: " + fmtFloat(currentDepthX, 3) + "mm)\n";
        }
        float startX = radiusStart - currentDepthX;
        float startZ = p.startZ;
        float endX = radiusStart;
        float endZ = p.startZ - currentDepthZ;
        gcode += "G1 X" + fmtFloat(startX, 3) + " Z" + fmtFloat(startZ, 3) + " F" + fmtFloat(p.feedRate, 0) + "\n";
        gcode += "G1 X" + fmtFloat(endX, 3) + " Z" + fmtFloat(endZ, 3) + " F" + fmtFloat(p.feedRate, 0) + "\n";
        gcode += "G0 Z" + fmtFloat(retractZ, 3) + "\n";
        gcode += "G0 X" + fmtFloat(retractX, 3) + "\n";
    }

    if (p.lastPassDepth > 0) {
        if (includeComments) gcode += "(Final Pass - Full profile: " + fmtFloat(p.lastPassDepth, 3) + "mm)\n";
        float startX = radiusStart - distanceX;
        float startZ = p.startZ;
        float endX = radiusStart;
        float endZ = p.startZ - distanceZ;
        gcode += "G1 X" + fmtFloat(startX, 3) + " Z" + fmtFloat(startZ, 3) + " F" + fmtFloat(p.lastFeedRate, 0) + "\n";
        gcode += "G1 X" + fmtFloat(endX, 3) + " Z" + fmtFloat(endZ, 3) + " F" + fmtFloat(p.lastFeedRate, 0) + "\n";
        gcode += "G0 Z" + fmtFloat(retractZ, 3) + "\n";
        gcode += "G0 X" + fmtFloat(retractX, 3) + "\n";
    }

    for (int i = 0; i < p.springPasses; ++i) {
        if (includeComments) gcode += "(Spring Pass: " + std::to_string(i+1) + ")\n";
        float startX = radiusStart - distanceX;
        float startZ = p.startZ;
        float endX = radiusStart;
        float endZ = p.startZ - distanceZ;
        gcode += "G1 X" + fmtFloat(startX, 3) + " Z" + fmtFloat(startZ, 3) + " F" + fmtFloat(p.lastFeedRate, 0) + "\n";
        gcode += "G1 X" + fmtFloat(endX, 3) + " Z" + fmtFloat(endZ, 3) + " F" + fmtFloat(p.lastFeedRate, 0) + "\n";
        gcode += "G0 Z" + fmtFloat(retractZ, 3) + "\n";
        gcode += "G0 X" + fmtFloat(retractX, 3) + "\n";
    }
    return gcode;
}

// -------------------------------------------------------------------
// Filettatura (Threading) – raggi
// -------------------------------------------------------------------
std::string generateThreadingGcode(const ThreadingParams &p, bool includeComments) {
    std::string gcode;
    if (includeComments) {
        gcode += "(THREADING G76)\n";
        gcode += std::string(p.rightHand ? "(Right-hand thread)\n" : "(Left-hand thread)\n");
        gcode += "(Pitch P = " + fmtFloat(p.pitch,3) + ")\n";
        gcode += "(Length Z = " + fmtFloat(p.length,3) + ")\n";
        gcode += "(Depth K = " + fmtFloat(p.threadDepth,3) + ")\n";
        gcode += "(FirstCut J = " + fmtFloat(p.firstCut,3) + ")\n";
        gcode += "(Offset I = " + fmtFloat(p.offset,3) + ")\n";
        if (p.springPasses > 0) gcode += "(SpringPasses H = " + std::to_string(p.springPasses) + ")\n";
        if (fabs(p.R - 1.0f) > 0.0001f) gcode += "(R = " + fmtFloat(p.R,3) + ")\n";
        if (p.L != 0) gcode += "(L = " + std::to_string(p.L) + ")\n";
        if (fabs(p.E) > 0.0001f) gcode += "(E = " + fmtFloat(p.E,3) + ")\n";
    }

    float targetZ = p.rightHand ? p.startZ - p.length : p.startZ + p.length;
    float startX = p.actualRadius + p.offset;
    float retractZ = p.rightHand ? p.startZ + 0.5f : p.startZ - 0.5f;

    gcode += "G0 X" + fmtFloat(startX,3) + "\n";
    gcode += "G0 Z" + fmtFloat(retractZ,3) + "\n";
    gcode += "G76";
    gcode += " P" + fmtFloat(p.pitch,3);
    gcode += " Z" + fmtFloat(targetZ,3);
    gcode += " I" + fmtFloat(p.offset,3);
    gcode += " J" + fmtFloat(p.firstCut,3);
    gcode += " K" + fmtFloat(p.threadDepth,3);
    if (p.springPasses > 0) gcode += " H" + std::to_string(p.springPasses);
    if (fabs(p.R - 1.0f) > 0.0001f) gcode += " R" + fmtFloat(p.R,3);
    if (p.L != 0) gcode += " L" + std::to_string(p.L);
    if (fabs(p.E) > 0.0001f) gcode += " E" + fmtFloat(p.E,3);
    gcode += "\n";
    gcode += "G0 Z" + fmtFloat(retractZ,3) + "\n";
    gcode += "G0 X" + fmtFloat(startX,3) + "\n";
    return gcode;
}

// -------------------------------------------------------------------
// Keyway (M800) – raggi
// -------------------------------------------------------------------
std::string generateKeywayGcode(const KeywayParams &kp, bool includeComments) {
    std::string gcode;
    if (includeComments) gcode += "(1) Modal safety\n";
    gcode += "G90\nG21\nM5\n";
    if (includeComments) gcode += "(1b) Feedrate\n";
    gcode += "F" + fmtFloat(kp.feed,3) + "\n";
    if (includeComments) gcode += "(2) Move mandrel to start position\n";
    gcode += "G0 A0\n";
    if (includeComments) gcode += "(3) Pause after mandrel positioning\n";
    gcode += "G4 P1\n";
    if (includeComments) gcode += "(4) Move to start position\n";
    gcode += "G0 X" + fmtFloat(kp.startX,3) + " Z" + fmtFloat(kp.startZ,3) + "\n";
    if (includeComments) gcode += "(5) Pause after X/Z positioning\n";
    gcode += "G4 P1\n";

    auto emitCycle = [&](int idx) {
        if (includeComments) gcode += "(6) Keyway cycle " + std::to_string(idx+1) + ")\n";
        gcode += "M800 D" + fmtFloat(kp.depth,3) + " Q" + fmtFloat(kp.length,3)
                 + " S" + fmtFloat(kp.toolWidth,3) + " P" + fmtFloat(kp.step,3)
                 + " R" + fmtFloat(kp.retract,3) + " L" + std::to_string(kp.repetitions)
                 + " H" + std::to_string(kp.returnToStart ? 1 : 0) + "\n";
    };

    if (!kp.multiple) {
        emitCycle(0);
    } else {
        for (int i = 0; i < kp.count; ++i) {
            emitCycle(i);
            if (i < kp.count - 1) {
                float rear = ((i+1) * kp.angleStep) / 360.0f;
                if (includeComments) gcode += "(6b) Pause before rotating spindle\n";
                gcode += "G4 P1\n";
                if (includeComments) gcode += "(6c) Rotate spindle for next keyway\n";
                gcode += "G0 A" + fmtFloat(rear,3) + "\n";
                if (includeComments) gcode += "(6d) Pause after spindle rotation\n";
                gcode += "G4 P1\n";
            }
        }
    }
    if (includeComments) gcode += "(7) End of program\n";
    gcode += "M30\n";
    return gcode;
}

// -------------------------------------------------------------------
// Blocco finale
// -------------------------------------------------------------------
std::string generateEndBlock(bool includeComments) {
    std::string gcode;
    if (includeComments) gcode += "(End block)\n";
    if (coolantEnabled) {
        if (includeComments) gcode += "(Coolant OFF)\n";
        gcode += "M9\n";
    }
    gcode += "M5\nM30\n";
    return gcode;
}

// ===================================================================
// Builder per Turning (tornitura)
// ===================================================================
GcodeBlock buildTurningBlock(const TurningParams &tp, bool includeComments) {
    SpindleParams sp;
    sp.useCSS = tp.useCSS;
    sp.spindleCW = tp.spindleCW;
    sp.spindleRPM = tp.spindleRPM;
    sp.spindleMmin = tp.spindleMmin;
    sp.radius = tp.actualRadius;
    GcodeBlock block;
    block.setup = generateSetupBlock(sp, includeComments);
    block.motion = selectTurningType(tp, block.type, includeComments);
    block.end = generateEndBlock(includeComments);
    return block;
}
// ===================================================================
// Builder per Facing (sfaccettatura)
// ===================================================================
GcodeBlock buildFacingBlock(const FacingParams &fp, bool includeComments) {
    SpindleParams sp;
    sp.useCSS = fp.useCSS;
    sp.spindleCW = fp.spindleCW;
    sp.spindleRPM = fp.spindleRPM;
    sp.spindleMmin = fp.spindleMmin;
    sp.radius = fp.startRadius;
    GcodeBlock block;
    block.setup = generateSetupBlock(sp, includeComments);
    block.motion = generateFacingGcode(fp, includeComments);
    block.end = generateEndBlock(includeComments);
    block.type = "Facing";
    return block;
}
// ===================================================================
// Builder per Parting (taglio)
// ===================================================================
GcodeBlock buildPartingBlock(const PartingParams &cp, bool includeComments) {
    SpindleParams sp;
    sp.useCSS = cp.useCSS;
    sp.spindleCW = cp.spindleCW;
    sp.spindleRPM = cp.spindleRPM;
    sp.spindleMmin = cp.spindleMmin;
    sp.radius = cp.startX;
    GcodeBlock block;
    block.setup = generateSetupBlock(sp, includeComments);
    block.motion = generatePartingGcode(cp, includeComments);
    block.end = generateEndBlock(includeComments);
    block.type = "Parting";
    return block;
}

// ===================================================================
// Builder per Hemispherical (emisferico)
// ===================================================================
GcodeBlock buildHemisphericalBlock(const HemisphericalParams &hp, bool includeComments) {
    SpindleParams sp;
    sp.useCSS = hp.useCSS;
    sp.spindleCW = hp.spindleCW;
    sp.spindleRPM = hp.spindleRPM;
    sp.spindleMmin = hp.spindleMmin;
    sp.radius = hp.startX;          // raggio di partenza per setup mandrino
    GcodeBlock block;
    block.setup = generateSetupBlock(sp, includeComments);
    if (hp.profileType) {
        // true = concavo
        block.motion = generateConcaveHemisphericalGcode(hp, includeComments);
        block.type = "ConcaveHemisphere"; 
    } else {
        // false = convesso
        block.motion = generateConvexHemisphericalGcode(hp, includeComments);
        block.type = "ConvexHemisphere";
    }
    block.end = generateEndBlock(includeComments);
    return block;
}

// ===================================================================
// Builder per Chamfer / Billet
// ===================================================================
GcodeBlock buildChamferBilletBlock(const ChamferBilletParams &cp, bool includeComments) {
    SpindleParams sp;
    sp.useCSS = cp.useCSS;
    sp.spindleCW = cp.spindleCW;
    sp.spindleRPM = cp.spindleRPM;
    sp.spindleMmin = cp.spindleMmin;
    sp.radius = cp.startX;
    GcodeBlock block;
    block.setup = generateSetupBlock(sp, includeComments);
    block.motion = generateChamferBilletGcode(cp, includeComments);
    block.end = generateEndBlock(includeComments);
    
    // Imposta il tipo specifico in base al parametro type
    if (cp.type == 1) {
        block.type = "Chamfer";
    } else if (cp.type == 2) {
        block.type = "Billet";
    }
    
    return block;
}

// ===================================================================
// Builder per Threading (filettatura)
// ===================================================================
GcodeBlock buildThreadingBlock(const ThreadingParams &tp, bool includeComments) {
    SpindleParams sp;
    sp.useCSS = false;
    sp.spindleCW = tp.spindleCW;
    sp.spindleRPM = tp.spindleRPM;
    sp.spindleMmin = 0.0f;
    sp.radius = tp.actualRadius;
    GcodeBlock block;
    block.setup = generateSetupBlock(sp, includeComments);
    block.motion = generateThreadingGcode(tp, includeComments);
    block.end = generateEndBlock(includeComments);
    
    // Imposta il tipo specifico: esterno/interno + destrorso/sinistrorso
    std::string orientation = tp.rightHand ? "Right" : "Left";
    std::string typeStr = tp.threadType ? "External" : "Internal";
    block.type = typeStr + orientation + "Thread";
    // Esempi: ExternalRightThread, ExternalLeftThread, InternalRightThread, InternalLeftThread
    
    return block;
}

// ===================================================================
// Builder per Keyway (cava chiavetta) – nessun mandrino, solo movimenti
// ===================================================================
GcodeBlock buildKeywayBlock(const KeywayParams &kp, bool includeComments) {
    // Setup minimo: G18 piano XZ, G21 mm, G90 assoluto
    std::string setup = "G18 G21 G90\n";
    if (includeComments) setup += "(Keyway operation - spindle not started)\n";
    GcodeBlock block;
    block.setup = setup;
    block.motion = generateKeywayGcode(kp, includeComments);
    block.end = generateEndBlock(includeComments);
    block.type = "Keyway";
    return block;
}

// -------------------------------------------------------------------
// Generatore principale (invia alla coda)
// -------------------------------------------------------------------
void GcodeGenerator(const GcodeBlock &block) {
    ESP_LOGI(TAG, "Generating G-code for type: %s", block.type.c_str());
    std::string fullGcode = block.setup + block.motion + block.end;
    if (fullGcode.empty()) {
        ESP_LOGE(TAG, "G-code vuoto");
        return;
    }
    
    // Stampa l'intero G-code (riga per riga) per debug
    ESP_LOGI(TAG, "=== FULL G-CODE START ===");
    size_t pos = 0;
    size_t len = fullGcode.length();
    while (pos < len) {
        size_t end = fullGcode.find('\n', pos);
        if (end == std::string::npos) end = len;
        std::string line = fullGcode.substr(pos, end - pos);
        if (!line.empty()) {
            ESP_LOGI(TAG, "%s", line.c_str());
        }
        pos = end + 1;
    }
    ESP_LOGI(TAG, "=== FULL G-CODE END ===");
    
    // Mostra il preview del G-code (invece di inviarlo direttamente)
    GcodeMonitorScene(block);
}

// -------------------------------------------------------------------
// Funzioni di calcolo
// -------------------------------------------------------------------
float calculateRPMfromCSS(float css_m_min, float diametro_mm) {
    if (diametro_mm <= 0) return 0;
    return (1000.0f * css_m_min) / (M_PI * diametro_mm);
}

float calculateMaxCSS(float diametro_mm, int maxRPM) {
    if (diametro_mm <= 0 || maxRPM <= 0) return 0;
    return (M_PI * diametro_mm * maxRPM) / 1000.0f;
}

float calculateTaperLength(float startDiam, float targetDiam, float angleDeg) {
    if (fabs(angleDeg) < 0.01f) return 0.0f;
    float deltaDiam = fabs(startDiam - targetDiam);
    float rad = angleDeg * M_PI / 180.0f;
    return deltaDiam / (2.0f * tan(rad));
}

int calculateCSSrpm(float diametro, float mmin) {
    if (diametro <= 0 || mmin <= 0) return 0;
    return (int)round((1000.0 * mmin) / (M_PI * diametro));
}