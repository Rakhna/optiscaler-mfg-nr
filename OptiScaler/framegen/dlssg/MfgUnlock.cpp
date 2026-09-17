#include "pch.h"
#include "MfgUnlock.h"
#include "../mfg/RenoMfg.h"

#include <Config.h>
#include <State.h>
#include <Util.h>
#include <misc/IdentifyGpu.h>

namespace MfgUnlock
{
Status LastStatus()
{
    Status st{};
    auto rst = RenoMfg::GetStatus();
    st.ModuleFound = rst.DlssgPatched || rst.InterposerHooked;
    st.AdvertiseMatched = rst.DlssgPatched;
    st.ValidateMatched = rst.DlssgPatched;
    st.NgxGateMatched = rst.DlssgPatched;
    st.EvaluateClampMatched = rst.DlssgPatched;
    st.KernelsRewritten = rst.TemporalPatched ? 1 : 0;
    st.StreamlineFound = rst.InterposerHooked;
    st.StreamlineCeilingPatched = rst.CeilingPatched;
    st.StreamlineCompiledCeiling = 3;
    st.StreamlineEffectiveCeiling = rst.EffectiveMultiplier;
    st.SnippetVersion = rst.Detail;
    return st;
}

void TryApply(HMODULE module)
{
    RenoMfg::Initialize();
}

bool TryPatchStreamline(HMODULE module)
{
    RenoMfg::Initialize();
    return RenoMfg::GetStatus().CeilingPatched;
}

void RestoreStreamline()
{
    RenoMfg::Shutdown();
}

bool Pending()
{
    return !RenoMfg::GetStatus().DlssgPatched;
}

unsigned int UnlockedMax()
{
    int mult = RenoMfg::GetMultiplier();
    return mult > 1 ? static_cast<unsigned int>(mult - 1) : 3u;
}
} // namespace MfgUnlock
