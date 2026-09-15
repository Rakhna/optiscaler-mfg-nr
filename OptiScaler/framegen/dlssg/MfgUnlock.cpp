// Adapted from y4my4my4m/OptiScaler_DLSSNR_Multipass_MFG, tag v4 (7b7220bb), GPL-3.0.
#include "pch.h"

#include "MfgUnlock.h"

#include <Config.h>
#include <State.h>
#include <Util.h>
#include <scanner/scanner.h>
#include <misc/IdentifyGpu.h>

#include <mutex>

namespace
{
// mov ebx,1 / mov r8d,3 / cmp edi,0x1b0 / cmovl r8d,ebx. The two counts and the architecture
// constant together are unique in the module; the wildcards cover nothing, they are here only to
// keep the shape readable.
constexpr std::string_view kAdvertisePattern = "BB 01 00 00 00 41 B8 03 00 00 00 81 FF B0 01 00 00 44 0F 4C C3";

// cmp eax,0x1b0 / jl / cmp ebx,3 / jbe. The only comparison against the architecture constant that
// is followed by a signed branch and a count test.
constexpr std::string_view kValidatePattern = "3D B0 01 00 00 7C ? 83 FB 03 76";

// Five generated frames, the count both patched sites carry.
constexpr uint8_t kMaxGeneratedFrames = 5;

// 310.9 restructured both gates. The count is no longer an immediate next to the comparison: the
// Blackwell branch starts at five and reads a configured value, and anything below Blackwell is sent
// to a branch that publishes one.
//     cmp ebp, 0x1b0
//     jl  ada          <- neutralised, so every card takes the Blackwell branch
//     mov edi, 0x5
constexpr std::string_view kAdvertisePattern309 = "81 FD B0 01 00 00 0F 8C ? ? ? ? BF 05 00 00 00";

// The capability flag in the same build is a setae rather than a branch.
//     cmp   eax, 0x1b0
//     setae al
constexpr std::string_view kValidatePattern309 = "3D B0 01 00 00 0F 93 C0";

std::mutex g_statusMutex;
MfgUnlock::Status g_status {};

uintptr_t UniqueAddress(HMODULE module, std::string_view pattern)
{
    const auto first = scanner::GetAddress(module, pattern);
    return first && !scanner::GetAddress(module, pattern, 0, first + 1) ? first : 0;
}

// The module's own file version, for the report. A signature that does not match is expected on a
// version nobody has looked at, and the version is the one thing that makes such a report actionable.
std::string ModuleVersion(HMODULE module)
{
    wchar_t path[MAX_PATH] {};

    if (GetModuleFileNameW(module, path, MAX_PATH) == 0)
        return {};

    version_t file {};
    version_t product {};

    if (!Util::GetFileVersion(path, &file, &product))
        return {};

    return std::format("{}.{}.{}", file.major, file.minor, file.patch);
}

bool WriteBytes(uintptr_t address, const uint8_t* bytes, size_t count)
{
    DWORD oldProtect = 0;

    if (!VirtualProtect(reinterpret_cast<LPVOID>(address), count, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        LOG_WARN("VirtualProtect failed at {:X}", address);
        return false;
    }

    std::memcpy(reinterpret_cast<void*>(address), bytes, count);

    DWORD ignored = 0;
    VirtualProtect(reinterpret_cast<LPVOID>(address), count, oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), count);

    return true;
}

std::string Hex(const uint8_t* bytes, size_t count)
{
    std::string out;

    for (size_t i = 0; i < count; ++i)
        out += std::format("{}{:02X}", i == 0 ? "" : " ", bytes[i]);

    return out;
}

// Rewrites count and neutralises the architecture clamp, so MultiFrameCountMax is published as five.
bool PatchAdvertise(HMODULE module)
{
    if (const auto at309 = UniqueAddress(module, kAdvertisePattern309); at309 != 0)
    {
        // The jl is a rel32, six bytes.
        const auto branchAt = at309 + 6;
        const uint8_t nop[] = { 0x0F, 0x1F, 0x44, 0x00, 0x00, 0x90 };

        LOG_INFO("MFG unlock: advertise (310.9) at {:X}, jl {} -> {}", at309,
                 Hex((const uint8_t*) branchAt, sizeof(nop)), Hex(nop, sizeof(nop)));

        return WriteBytes(branchAt, nop, sizeof(nop));
    }

    const auto address = UniqueAddress(module, kAdvertisePattern);

    if (address == 0)
    {
        LOG_WARN("MFG unlock: the advertise signature did not match, nvngx_dlssg.dll left alone");
        return false;
    }

    // Offsets within the matched sequence: the r8d immediate, and the cmovl.
    const auto countAt = address + 7;
    const auto cmovAt = address + 17;

    const uint8_t count[] = { kMaxGeneratedFrames };
    const uint8_t nop[] = { 0x0F, 0x1F, 0x40, 0x00 };

    LOG_INFO("MFG unlock: advertise at {:X}, count {} -> {}, cmovl {} -> {}", address,
             *(const uint8_t*) countAt, kMaxGeneratedFrames, Hex((const uint8_t*) cmovAt, sizeof(nop)),
             Hex(nop, sizeof(nop)));

    return WriteBytes(countAt, count, sizeof(count)) && WriteBytes(cmovAt, nop, sizeof(nop));
}

// Drops the Ada branch and raises the accepted count, so a request for five is not rejected.
bool PatchValidate(HMODULE module)
{
    if (const auto at309 = UniqueAddress(module, kValidatePattern309); at309 != 0)
    {
        // setae al -> mov al, 1, so the flag is set whatever the architecture reports.
        const auto setAt = at309 + 5;
        const uint8_t always[] = { 0xB0, 0x01, 0x90 };

        LOG_INFO("MFG unlock: validate (310.9) at {:X}, setae {} -> {}", at309,
                 Hex((const uint8_t*) setAt, sizeof(always)), Hex(always, sizeof(always)));

        return WriteBytes(setAt, always, sizeof(always));
    }

    const auto address = UniqueAddress(module, kValidatePattern);

    if (address == 0)
    {
        LOG_WARN("MFG unlock: the validate signature did not match, nvngx_dlssg.dll left alone");
        return false;
    }

    // Offsets within the matched sequence: the jl, and the immediate of the count test behind it.
    const auto branchAt = address + 5;
    const auto countAt = address + 9;

    const uint8_t nop[] = { 0x90, 0x90 };
    const uint8_t count[] = { kMaxGeneratedFrames };

    LOG_INFO("MFG unlock: validate at {:X}, jl {} -> {}, count {} -> {}", address,
             Hex((const uint8_t*) branchAt, sizeof(nop)), Hex(nop, sizeof(nop)), *(const uint8_t*) countAt,
             kMaxGeneratedFrames);

    return WriteBytes(branchAt, nop, sizeof(nop)) && WriteBytes(countAt, count, sizeof(count));
}


// Gives Ada the Blackwell kernels the module already carries.
//
// nvngx_dlssg.dll ships two builds of the interpolation kernels. Kernel_EstimateIntermMvecsScatter
// reads three f32 fields of its parameter block on sm_120 and one on sm_89, so on Ada every generated
// frame is placed at the same point between the two real ones: the world does not advance between
// them while the interface, composited once per present, does. At 2X there is one frame and nothing
// to distinguish; above it that is the whole symptom.
//
// The sm_120 module uses no instruction Ada lacks. So per container: the Blackwell PTX image is
// relabelled sm_89, its .target directive is rewritten in place (".target sm_120" and
// ".target sm_89 " are both fourteen bytes, and the directive sits in the literal run at the head of
// the LZ4 stream), and the images that were sm_89 -- the Ada PTX and its SASS -- are relabelled to an
// architecture that does not exist so the driver cannot select them. The driver then JITs Blackwell's
// kernel when it asks for Ada's.
//
// Nothing is copied in and no payload changes length. A container without both images is left alone.
constexpr uint32_t kArchAda = 89;
constexpr uint32_t kArchBlackwell = 120;

// No such shader model. Parks an image where nothing will ask for it.
constexpr uint32_t kArchParked = 122;

// Offsets inside a fatbin image header: payload length, and the architecture the image answers for.
constexpr size_t kImagePayloadSize = 8;
constexpr size_t kImageArch = 28;

unsigned int RewriteBlackwellKernels(HMODULE module)
{
    if (!module)
        return 0;
    auto base = reinterpret_cast<uint8_t*>(module);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return 0;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return 0;
    auto section = IMAGE_FIRST_SECTION(nt);

    const uint8_t magic[] = { 0x50, 0xED, 0x55, 0xBA };
    unsigned int rewritten = 0;

    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        const auto& s = section[i];

        if (s.Characteristics & IMAGE_SCN_MEM_EXECUTE)
            continue;

        uint8_t* start = base + s.VirtualAddress;
        uint8_t* end = start + s.Misc.VirtualSize;

        for (uint8_t* c = std::search(start, end, magic, magic + sizeof(magic)); c < end;
             c = std::search(c + 1, end, magic, magic + sizeof(magic)))
        {
            if (end - c < 16)
                break;

            const auto headerSize = *reinterpret_cast<const uint16_t*>(c + 6);
            const auto fatSize = *reinterpret_cast<const uint64_t*>(c + 8);

            if (headerSize != 0x10 || fatSize == 0 || fatSize > (uint64_t) (end - c - 16))
                continue;

            uint8_t* blackwell = nullptr;
            size_t blackwellHeader = 0;
            size_t blackwellPayload = 0;
            std::vector<uint8_t*> ada;
            bool valid = true;

            for (uint8_t* image = c + 16; image < c + 16 + fatSize;)
            {
                const auto remaining = (uint64_t) (c + 16 + fatSize - image);
                if (remaining < kImageArch + sizeof(uint32_t))
                {
                    valid = false;
                    break;
                }
                const auto kind = *reinterpret_cast<const uint16_t*>(image);
                const auto imageHeader = *reinterpret_cast<const uint32_t*>(image + 4);
                const auto payload = *reinterpret_cast<const uint64_t*>(image + kImagePayloadSize);
                const auto arch = *reinterpret_cast<const uint32_t*>(image + kImageArch);

                if (imageHeader < kImageArch + sizeof(uint32_t) || imageHeader > remaining ||
                    payload == 0 || payload > remaining - imageHeader)
                {
                    valid = false;
                    break;
                }

                // kind 1 is PTX, 2 is a cubin. Only the PTX can be retargeted; the cubin is parked.
                if (kind == 1 && arch == kArchBlackwell)
                {
                    blackwell = image;
                    blackwellHeader = imageHeader;
                    blackwellPayload = payload;
                }
                else if (arch == kArchAda)
                {
                    ada.push_back(image);
                }

                image += imageHeader + payload;
            }

            if (!valid || blackwell == nullptr || ada.empty())
                continue;

            const char from[] = ".target sm_120";
            const char to[] = ".target sm_89 ";
            static_assert(sizeof(from) == sizeof(to), "the directive rewrite must not change length");

            uint8_t* body = blackwell + blackwellHeader;
            uint8_t* bodyEnd = body + blackwellPayload;
            auto at = std::search(body, bodyEnd, from, from + sizeof(from) - 1);

            if (at == bodyEnd)
                continue;

            const uint32_t ada89 = kArchAda;
            const uint32_t parked = kArchParked;
            // Prepare one complete container first. A failed protection change must not leave
            // its PTX target and architecture headers disagreeing, or count a partial rewrite.
            std::vector<uint8_t> patched(c, c + 16 + fatSize);
            std::memcpy(patched.data() + (at - c), to, sizeof(to) - 1);
            std::memcpy(patched.data() + (blackwell + kImageArch - c), &ada89, sizeof(ada89));
            for (uint8_t* image : ada)
                std::memcpy(patched.data() + (image + kImageArch - c), &parked, sizeof(parked));
            if (WriteBytes(reinterpret_cast<uintptr_t>(c), patched.data(), patched.size()))
                ++rewritten;
        }
    }

    LOG_INFO("MFG unlock: {} kernel containers answer Ada with the Blackwell image", rewritten);

    return rewritten;
}

// ------------------------------------------------------- Streamline plugin ceiling patch
HMODULE g_ceilingModule = nullptr;
unsigned char* g_ceilingSite = nullptr;
unsigned char g_ceilingOriginal = 0;
unsigned char g_ceilingCmovOriginal = 0;
bool g_ceilingPatched = false;

// Scan executable sections for the Streamline plugin clamp:
//     mov edx, <compiled ceiling>   ; BA ?? 00 00 00
//     cmp ecx, edx                  ; 3B CA
//     cmovb edx, ecx                ; 0F 42 D1  <- lowers ceiling to NGX's reported 1 frame
//
// Rewriting the final byte 0xD1 -> 0xD2 turns this into cmovb edx, edx: identical 3-byte size,
// no lowering of the compiled ceiling.
bool PatchStreamlinePlugin(HMODULE mod)
{
    if (mod == nullptr)
        return false;

    if (g_ceilingPatched)
        return true;

    auto base = reinterpret_cast<uint8_t*>(mod);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;

    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;

    const unsigned char tail[] = { 0x3B, 0xCA, 0x0F, 0x42, 0xD1 };
    unsigned char* found = nullptr;
    size_t hits = 0;
    auto section = IMAGE_FIRST_SECTION(nt);

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        const auto& s = section[i];
        if (!(s.Characteristics & IMAGE_SCN_MEM_EXECUTE))
            continue;

        uint8_t* start = base + s.VirtualAddress;
        const size_t size = s.Misc.VirtualSize;
        if (size < 10)
            continue;

        for (size_t off = 0; off + 10 <= size; ++off)
        {
            if (start[off] != 0xBA)
                continue;
            if (start[off + 2] != 0 || start[off + 3] != 0 || start[off + 4] != 0)
                continue;
            if (std::memcmp(start + off + 5, tail, sizeof(tail)) != 0)
                continue;

            const unsigned char ceiling = start[off + 1];
            if (ceiling == 0 || ceiling > 8)
                continue;

            if (found == nullptr)
                found = start + off;
            ++hits;
        }
    }

    if (hits != 1 || found == nullptr)
    {
        LOG_WARN("MFG unlock: found {} frame-count clamps in Streamline plugin (expected 1); left alone", hits);
        return false;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(found, 10, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        LOG_WARN("MFG unlock: VirtualProtect failed for Streamline clamp at {:X}", (uintptr_t) found);
        return false;
    }

    g_ceilingModule = mod;
    g_ceilingSite = found;
    g_ceilingOriginal = found[1];
    g_ceilingCmovOriginal = found[9];

    // cmovb edx, ecx (0x0F 0x42 0xD1) -> cmovb edx, edx (0x0F 0x42 0xD2)
    found[9] = 0xD2;

    DWORD ignored = 0;
    VirtualProtect(found, 10, oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), found, 10);

    {
        std::lock_guard<std::mutex> lock(g_statusMutex);
        g_ceilingPatched = true;
        g_status.StreamlineFound = true;
        g_status.StreamlineCeilingPatched = true;
        g_status.StreamlineCompiledCeiling = g_ceilingOriginal;
        g_status.StreamlineEffectiveCeiling = g_ceilingOriginal;
    }

    LOG_INFO("MFG unlock: stopped the DLSS-G plugin from lowering its compiled ceiling of {} generated frame(s) to the stale NGX device value (effective maximum {}x)",
             g_ceilingOriginal, g_ceilingOriginal + 1);

    return true;
}
} // namespace

bool MfgUnlock::TryPatchStreamline(HMODULE requestedModule)
{
    if (g_ceilingPatched)
        return true;

    static bool s_patchAttempted = false;
    if (s_patchAttempted && requestedModule == nullptr)
        return false;

    if (!Config::Instance()->FGDLSSGAdaMfgUnlock.value_or_default() ||
        Config::Instance()->FGDLSSGAmpereMfgUnlock.value_or_default() ||
        State::Instance().externalFrameGeneration)
        return false;

    const auto& gpu = IdentifyGpu::getPrimaryGpu();
    if (gpu.vendorId != VendorId::Nvidia || gpu.nvidiaArchInfo.architecture_id != NV_GPU_ARCHITECTURE_AD100)
        return false;

    HMODULE mod = requestedModule ? requestedModule : GetModuleHandleW(L"sl.dlss_g.dll");
    if (mod != nullptr)
    {
        s_patchAttempted = true;
        return PatchStreamlinePlugin(mod);
    }

    return false;
}

void MfgUnlock::RestoreStreamline()
{
    if (!g_ceilingPatched || g_ceilingSite == nullptr)
        return;

    DWORD oldProtect = 0;
    if (VirtualProtect(g_ceilingSite, 10, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        g_ceilingSite[9] = g_ceilingCmovOriginal;
        DWORD ignored = 0;
        VirtualProtect(g_ceilingSite, 10, oldProtect, &ignored);
        FlushInstructionCache(GetCurrentProcess(), g_ceilingSite, 10);
    }

    g_ceilingModule = nullptr;
    g_ceilingSite = nullptr;

    {
        std::lock_guard<std::mutex> lock(g_statusMutex);
        g_ceilingPatched = false;
        g_status.StreamlineCeilingPatched = false;
    }
}

void MfgUnlock::TryApply(HMODULE requestedModule)
{
    // Latches once nvngx_dlssg.dll is present; before that every call rescans for it.
    static bool snippetDone = false;

    if (snippetDone && requestedModule == nullptr)
    {
        TryPatchStreamline();
        return;
    }

    if (!Config::Instance()->FGDLSSGAdaMfgUnlock.value_or_default() ||
        Config::Instance()->FGDLSSGAmpereMfgUnlock.value_or_default() ||
        State::Instance().externalFrameGeneration)
        return;

    const auto& gpu = IdentifyGpu::getPrimaryGpu();
    // The kernel retarget is Ada-specific. Do not patch Ampere/Turing or change Blackwell's working path.
    if (gpu.vendorId != VendorId::Nvidia || gpu.nvidiaArchInfo.architecture_id != NV_GPU_ARCHITECTURE_AD100)
        return;

    if (!snippetDone)
    {
        if (auto module = requestedModule ? requestedModule : GetModuleHandleW(L"nvngx_dlssg.dll"); module != nullptr)
        {
            snippetDone = true;
            auto version = ModuleVersion(module);
            {
                std::lock_guard<std::mutex> lock(g_statusMutex);
                g_status.ModuleFound = true;
                g_status.SnippetVersion = version;
            }

            // Validate both gates before touching either. Ambiguous/unknown versions remain unmodified.
            const bool knownGates =
                (UniqueAddress(module, kAdvertisePattern309) && UniqueAddress(module, kValidatePattern309)) ||
                (UniqueAddress(module, kAdvertisePattern) && UniqueAddress(module, kValidatePattern));
            if (!knownGates)
            {
                LOG_WARN("MFG unlock: unsupported or ambiguous DLSSG {} signatures; left unchanged",
                         version);
                return;
            }

            // Default on where it applies: below Blackwell the unlock alone produces frames that do
            // not advance the picture, so the two belong together. dlssCapable is set from the same
            // field, so an architecture that never reported leaves this off.
            const bool preBlackwell = gpu.vendorId == VendorId::Nvidia &&
                                      gpu.nvidiaArchInfo.architecture_id >= NV_GPU_ARCHITECTURE_TU100 &&
                                      gpu.nvidiaArchInfo.architecture_id <= NV_GPU_ARCHITECTURE_AD100;

            unsigned int kernelsRewritten = 0;
            if (Config::Instance()->FGDLSSGAdaBlackwellKernels.value_or(preBlackwell))
                kernelsRewritten = RewriteBlackwellKernels(module);

            if (kernelsRewritten == 0)
            {
                LOG_WARN("MFG unlock: no compatible interpolation kernels; frame-count gates left unchanged");
                return;
            }
            const bool advertise = PatchAdvertise(module);
            const bool validate = PatchValidate(module);

            {
                std::lock_guard<std::mutex> lock(g_statusMutex);
                g_status.KernelsRewritten = kernelsRewritten;
                g_status.AdvertiseMatched = advertise;
                g_status.ValidateMatched = validate;
            }

            if (advertise && validate)
                LOG_INFO("MFG unlock: nvngx_dlssg.dll patched for {} generated frames", kMaxGeneratedFrames);
            else
                LOG_WARN("MFG unlock: nvngx_dlssg.dll incomplete, advertise {}, validate {}", advertise, validate);
        }
    }

    // Also attempt Streamline plugin patch if already loaded
    TryPatchStreamline();
}

unsigned int MfgUnlock::UnlockedMax()
{
    const auto status = LastStatus();

    return status.AdvertiseMatched && status.ValidateMatched && status.KernelsRewritten > 0
               ? kMaxGeneratedFrames : 0;
}

bool MfgUnlock::Pending()
{
    if (!Config::Instance()->FGDLSSGAdaMfgUnlock.value_or_default() ||
        Config::Instance()->FGDLSSGAmpereMfgUnlock.value_or_default() ||
        State::Instance().externalFrameGeneration)
        return false;

    {
        std::lock_guard<std::mutex> lock(g_statusMutex);
        if (g_status.ModuleFound)
            return false;
    }

    const auto& gpu = IdentifyGpu::getPrimaryGpu();
    return gpu.vendorId == VendorId::Nvidia && gpu.nvidiaArchInfo.architecture_id == NV_GPU_ARCHITECTURE_AD100;
}

MfgUnlock::Status MfgUnlock::LastStatus()
{
    std::lock_guard<std::mutex> lock(g_statusMutex);
    return g_status;
}
