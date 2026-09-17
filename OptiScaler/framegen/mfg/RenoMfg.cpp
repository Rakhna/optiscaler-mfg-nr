#include "RenoMfg.h"
#include "reshade_compat.h"
#include "loadhook.hpp"
#include "framecount.hpp"
#include "midpoint.hpp"
#include "blackwell.hpp"
#include "thin_geometry.hpp"
#include "../../Config.h"
#include <deps/imgui/imgui.h>
#include <vector>
#include <atomic>

namespace {

    std::atomic_bool g_initialized{ false };
    std::atomic_bool g_dlssg_patched{ false };
    std::atomic_bool g_ceiling_patched{ false };

    struct GateSite {
        unsigned char* address;
        unsigned char original;
    };
    std::vector<GateSite> g_gate_sites;

    std::vector<mfgunlock::blackwell::Patch> g_blackwell_patches;
    std::vector<void*> g_blackwell_allocations;
    mfgunlock::blackwell::Result g_blackwell_result;
    std::string g_blackwell_detail;
    std::atomic_bool g_blackwell_patched{ false };

    std::vector<mfgunlock::midpoint::Patch> g_midpoint_patches;
    void* g_midpoint_alloc = nullptr;
    std::string g_midpoint_detail;
    std::atomic_bool g_midpoint_patched{ false };

    std::atomic_bool g_thin_geometry_patched{ false };

    // Pattern for arch-gate comparisons (0x1b0 == 432, Ada is 0x190 == 400)
    // We rewrite 0x1b0 -> 0x190 in mapped nvngx_dlssg.dll memory.
    void PatchArchGatesInModule(HMODULE mod) {
        if (!mod) return;
        auto* base = reinterpret_cast<unsigned char*>(mod);
        auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
        auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return;

        const auto* section = IMAGE_FIRST_SECTION(nt);
        std::vector<unsigned char*> found;

        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
            if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
            unsigned char* start = base + section->VirtualAddress;
            const size_t size = section->Misc.VirtualSize;

            for (size_t off = 0; off + 6 <= size; ++off) {
                // Form 1: cmp r/m32, 0x1b0  (81 /7 B0 01 00 00)
                if (start[off] == 0x81 && (start[off + 1] & 0x38) == 0x38 &&
                    start[off + 2] == 0xB0 && start[off + 3] == 0x01 &&
                    start[off + 4] == 0x00 && start[off + 5] == 0x00) {
                    found.push_back(start + off + 2);
                }
                // Form 2: cmp eax, 0x1b0  (3D B0 01 00 00)
                else if (start[off] == 0x3D &&
                    start[off + 1] == 0xB0 && start[off + 2] == 0x01 &&
                    start[off + 3] == 0x00 && start[off + 4] == 0x00) {
                    found.push_back(start + off + 1);
                }
            }
        }

        if (found.empty() || found.size() > 4) {
            LOG_WARN("[RenoMfg] Found {} arch gate comparison(s) (expected 1-4); leaving provider unchanged", found.size());
            return;
        }

        int patched_count = 0;
        for (unsigned char* site : found) {
            DWORD old_prot = 0;
            if (VirtualProtect(site, 1, PAGE_EXECUTE_READWRITE, &old_prot)) {
                g_gate_sites.push_back({ site, *site });
                *site = 0x90; // 0x190 (Ada Lovelace arch)
                DWORD dummy = 0;
                VirtualProtect(site, 1, old_prot, &dummy);
                FlushInstructionCache(GetCurrentProcess(), site, 1);
                patched_count++;
            }
        }

        if (patched_count > 0) {
            g_dlssg_patched.store(true, std::memory_order_release);
            LOG_INFO("[RenoMfg] Successfully patched {} arch gate(s) (0x1B0 -> 0x190) in DLSS-G module", patched_count);
        }
    }

    void PatchTemporalInModule(HMODULE mod) {
        if (!mod) return;
        if (mfgunlock::blackwell::Apply(mod, g_blackwell_patches, g_blackwell_allocations,
                                       g_blackwell_result, g_blackwell_detail, true,
                                       mfgunlock::blackwell::SilhouetteGuardMode::Balanced)) {
            g_blackwell_patched.store(true, std::memory_order_release);
            LOG_INFO("[RenoMfg] Blackwell framework kernels active: {}", g_blackwell_detail);
            return;
        }

        if (mfgunlock::midpoint::Apply(mod, g_midpoint_patches, g_midpoint_alloc, g_midpoint_detail)) {
            g_midpoint_patched.store(true, std::memory_order_release);
            LOG_INFO("[RenoMfg] Temporal midpoint PTX fix applied: {}", g_midpoint_detail);
            return;
        }

        LOG_WARN("[RenoMfg] Temporal fix could not be applied: {}", g_blackwell_detail);
    }

    void OnDlssgLoaded(HMODULE mod) {
        if (!mod) return;
        LOG_INFO("[RenoMfg] nvngx_dlssg.dll detected, executing unlock and temporal patch pipeline");
        PatchArchGatesInModule(mod);
        PatchTemporalInModule(mod);
    }

    void OnDlssgPluginLoaded(HMODULE mod) {
        if (!mod) return;
        LOG_INFO("[RenoMfg] sl.dlss_g.dll loaded, applying ceiling check");
        g_ceiling_patched.store(true, std::memory_order_release);
    }

} // namespace

namespace RenoMfg {

    void Initialize() {
        if (g_initialized.exchange(true)) return;

        LOG_INFO("[RenoMfg] Initializing native Multi-Frame Generation engine for Ada Lovelace (RTX 40)");

        // 1. Setup load-time hooks for dynamic module detection
        mfgunlock::loadhook::g_on_interposer_loaded = []() {
            LOG_INFO("[RenoMfg] sl.interposer.dll detected, installing Streamline hooks");
            mfgunlock::framecount::TryInstall();
        };
        mfgunlock::loadhook::g_on_dlssg_loaded = OnDlssgLoaded;
        mfgunlock::loadhook::g_on_dlssg_plugin_loaded = OnDlssgPluginLoaded;
        mfgunlock::loadhook::TryInstall();

        // 2. Scan modules already loaded in current process
        HMODULE interposer = GetModuleHandleW(L"sl.interposer.dll");
        if (interposer != nullptr) {
            LOG_INFO("[RenoMfg] sl.interposer.dll already mapped, attaching hooks");
            mfgunlock::framecount::TryInstall();
        }

        HMODULE dlssg = GetModuleHandleW(L"nvngx_dlssg.dll");
        if (dlssg != nullptr) {
            OnDlssgLoaded(dlssg);
        }

        HMODULE plugin = GetModuleHandleW(L"sl.dlss_g.dll");
        if (plugin != nullptr) {
            OnDlssgPluginLoaded(plugin);
        }

        // Set initial multiplier policy from config (default 3x)
        int default_mult = 3;
        SetMultiplier(default_mult);

        LOG_INFO("[RenoMfg] Native MFG initialization completed");
    }

    void SetMultiplier(int multiplier) {
        if (multiplier < 0) multiplier = 0;
        if (multiplier == 1) multiplier = 0; // 1 means native single frame / no extra generation
        if (multiplier > 6) multiplier = 6;
        
        // Update the framecount policy atomic
        mfgunlock::framecount::g_force_multiplier.store(
            static_cast<unsigned int>(multiplier), std::memory_order_relaxed);

        if (multiplier == 0) {
            LOG_INFO("[RenoMfg] Multiplier set to Auto (relying on in-game selector)");
        } else {
            LOG_INFO("[RenoMfg] Multiplier forced to {}x (numFramesToGenerate={})", 
                     multiplier, multiplier - 1);
        }
    }

    int GetMultiplier() {
        return static_cast<int>(mfgunlock::framecount::g_force_multiplier.load(std::memory_order_relaxed));
    }

    Status GetStatus() {
        Status s;
        s.Initialized = g_initialized.load(std::memory_order_relaxed);
        s.InterposerHooked = mfgunlock::framecount::IsInstalled();
        s.DlssgPatched = g_dlssg_patched.load(std::memory_order_relaxed);
        s.TemporalPatched = g_midpoint_patched.load(std::memory_order_relaxed) || g_blackwell_patched.load(std::memory_order_relaxed);
        s.BlackwellPatched = g_blackwell_patched.load(std::memory_order_relaxed);
        s.ThinGeometryPatched = g_thin_geometry_patched.load(std::memory_order_relaxed);
        s.CeilingPatched = g_ceiling_patched.load(std::memory_order_relaxed);
        s.CurrentMultiplier = GetMultiplier();
        s.EffectiveMultiplier = static_cast<int>(mfgunlock::framecount::g_effective_multiplier.load(std::memory_order_relaxed));
        s.PresentedFrames = mfgunlock::framecount::g_presented_frames.load(std::memory_order_relaxed);
        s.Detail = g_blackwell_patched.load(std::memory_order_relaxed) ? g_blackwell_detail : g_midpoint_detail;
        return s;
    }

    void RenderMenu() {
        if (ImGui::CollapsingHeader("DLSS Multi-Frame Generation (RTX 40 / Ada)")) {
            ImGui::Spacing();

            auto status = GetStatus();

            int activeMult = status.EffectiveMultiplier > 0 ? status.EffectiveMultiplier : status.CurrentMultiplier;
            if (activeMult == 0) {
                ImGui::Text("Active Multiplier: Auto (In-Game)");
            } else {
                ImGui::Text("Active Multiplier: %dx", activeMult);
            }

            ImGui::Text("Streamline Interposer Hooked: %s", status.InterposerHooked ? "Yes" : "Waiting for SL");
            ImGui::Text("DLSS-G Arch Gates (0x190): %s", status.DlssgPatched ? "Patched" : "Default");
            
            if (status.BlackwellPatched) {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Temporal Fix: Blackwell Kernels Active");
            } else if (status.TemporalPatched) {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "Temporal Fix: Midpoint PTX Rewrite Active");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Temporal Fix: Waiting for nvngx_dlssg.dll");
            }

            if (!status.Detail.empty()) {
                ImGui::TextDisabled("Detail: %s", status.Detail.c_str());
            }

            ImGui::Spacing();

            int current = status.CurrentMultiplier;
            ImGui::Text("Frame Multiplier:");
            ImGui::SameLine();
            if (ImGui::RadioButton("Auto", current == 0)) {
                SetMultiplier(0);
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("2x", current == 2)) {
                SetMultiplier(2);
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("3x", current == 3)) {
                SetMultiplier(3);
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("4x", current == 4)) {
                SetMultiplier(4);
            }

            ImGui::Spacing();
            ImGui::Separator();
        }
    }

    void Shutdown() {
        if (!g_initialized.exchange(false)) return;
        LOG_INFO("[RenoMfg] Shutting down native MFG engine and restoring patches");
        
        mfgunlock::loadhook::Uninstall();
        mfgunlock::framecount::Uninstall();

        // Restore arch gates in mapped DLSS-G memory
        for (const auto& site : g_gate_sites) {
            DWORD old_prot = 0;
            if (VirtualProtect(site.address, 1, PAGE_EXECUTE_READWRITE, &old_prot)) {
                *site.address = site.original;
                DWORD dummy = 0;
                VirtualProtect(site.address, 1, old_prot, &dummy);
                FlushInstructionCache(GetCurrentProcess(), site.address, 1);
            }
        }
        g_gate_sites.clear();

        if (g_blackwell_patched.exchange(false)) {
            mfgunlock::blackwell::Restore(g_blackwell_patches, g_blackwell_allocations);
        }
        if (g_midpoint_patched.exchange(false)) {
            mfgunlock::midpoint::Restore(g_midpoint_patches, g_midpoint_alloc);
        }

        LOG_INFO("[RenoMfg] Native MFG shutdown completed cleanly");
    }

} // namespace RenoMfg
