#ifndef TRACKING_H
#define TRACKING_H

#include "GcodeMonitor.h"
#include <functional>
#include <string>

class GcodeTrackerEngine {
public:
    GcodeTrackerEngine(const GcodeBlockRanges& ranges,
                       const std::string& allGcode,
                       const std::string& gcodeType,
                       std::function<void()> requestNextBlock,
                       int totalBlocks);

    void run();
    void trackMotionBlock();
    bool trackSetupBlock();
    void trackMotionUniversal();
    bool trackEndBlock();
    void trackMotionKeyway();

    void advanceLine(int absIdx, int nextAbsIdx);
    int getCurrentBlockIndex() const { return currentBlockIndex; }
    void advanceBlock() { currentBlockIndex++; }
    void setCurrentMotionCount(int v) { currentMotionCount = v; }
    void addToMotionCount(int delta) { currentMotionCount += delta; }

private:
    bool skipSetup = false;
    bool skipEnd = false;
    int preAnalyzeFirstMotionLines();

    GcodeBlockRanges ranges;
    std::string allGcode;
    std::string gcodeType;

    std::vector<int> nonSignificantLines;
    std::function<void()> requestNextBlockCallback;

    int currentBlockIndex = 0;
    int totalBlocks = 0;
    bool allBlocksSent = false;
    unsigned long lastBlockSendTime = 0;
    unsigned long lastLnTime = 0;        // Aggiunta per il timer di stallo Ln
    int currentMotionCount = 0;

    bool handleBlockPacing(int absIdx, const std::string& lineCurr);
};

extern GcodeTrackerEngine* trackerEngineInstance;

#endif