#pragma once

#include <cstdint>
#include <string>

namespace RenoMfg {

    struct Status {
        bool Initialized = false;
        bool InterposerHooked = false;
        bool DlssgPatched = false;
        bool TemporalPatched = false;
        bool BlackwellPatched = false;
        bool ThinGeometryPatched = false;
        bool CeilingPatched = false;
        int CurrentMultiplier = 3;
        int EffectiveMultiplier = 1;
        uint32_t PresentedFrames = 0;
        std::string Detail;
    };

    // Initializes the load hooks and searches memory for Streamline/DLSS-G
    void Initialize();

    // Sets the target multiplier (2 = 2x, 3 = 3x, 4 = 4x)
    void SetMultiplier(int multiplier);

    // Gets the current configuration multiplier
    int GetMultiplier();

    // Returns current operational status
    Status GetStatus();

    // Renders the ImGui settings in OptiScaler menu
    void RenderMenu();

    // Cleans up Detours and memory patches on shutdown
    void Shutdown();

} // namespace RenoMfg
