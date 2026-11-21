#pragma once
#include <Geode/Geode.hpp>
#include <functional>
#include "../core/Core.hpp"

namespace ad::ed {

struct State {
    ad::Settings settings;
    ad::Analysis analysis;
    std::vector<int> targets;                 // structure ids that will be decorated
    std::vector<ad::Variation> variations;
    int selected = 0;                         // currently previewed variation
    bool previewing = false;
    bool busy = false;                        // object creation in progress
    bool fullMode = false;                    // last generation came from Full Auto
    uint64_t salt = 0;                        // incremented by "Generate More"
    std::string status = "Ready.";
    float progress = 0.f;                     // 0..1 while objects are being created
    std::vector<geode::Ref<GameObject>> preview;
    std::vector<std::vector<geode::Ref<GameObject>>> history; // applied batches (this session)
};

State& state();
bool inEditor();

// Scan the level (or the current editor selection) and fill state().analysis.
bool scan(bool selectionOnly, std::string& err);

// Generate `count` variations for state().targets (or the whole level when full == true).
bool generate(bool full, int count, std::string& err);

// Preview variation `idx` by creating the real objects (undone again by cancelPreview()).
void showPreview(int idx);
void cancelPreview();
bool applyPreview();            // commits the preview as ONE editor undo step
bool removeLastBatch();         // removes the last applied batch created by the mod

std::string catalogReport();    // which object IDs the roles resolved to
void rebuildCatalog();

// High level actions shared by UI and hotkeys.
void actionDecorateSelection();
void actionFullAuto();
void actionMoreVariations();

} // namespace ad::ed
