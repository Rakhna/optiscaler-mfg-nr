# OptiScaler (DLSS Neural Rendering + Multi-Frame Generation)

A unified OptiScaler build combining:
1. **NVIDIA DLSS 5 Neural Rendering (DLSS-NR)**: Pre-SR multipass AI detail, lighting, and colour reconstruction.
2. **Multi-Frame Generation (MFG)**: Built-in 100% in-memory unlock for Ada Lovelace (RTX 40 series) supporting up to 6x (2x/3x/4x/6x) with Reflex sync, plus Ampere/Turing (RTX 20/30) SM86/SM75 loader support.

**[Download the latest release](https://github.com/Rakhna/optiscaler-mfg-nr/releases/tag/nightly)** · [Setup guide](INSTALL-DLSSNR.md) · [Issues](https://github.com/Rakhna/optiscaler-mfg-nr/issues)

---

## Key Features

### Neural Rendering (DLSS-NR)
- Adjust the strength, lighting, detail, and colour of the neural reconstruction pass.
- Use separate adjustments for skin protection and general scenery.
- Run the effect before spatial reconstruction (Pre-SR Multipass) or after upscaling.
- Clean up specular shimmer, fine geometric noise, and ray reconstruction temporal artifacts before frame generation.
- Configurable precision modes (FP8 native / FP8+NVFP4 hybrid).

### Multi-Frame Generation (MFG Unlock)
- **RTX 40 Series (Ada Lovelace)**: In-memory dynamic patch of `sl.dlss_g.dll` enabling 2x, 3x, 4x, and up to 6x frame generation without touching signed binaries on disk. Fully synchronized with NVIDIA Reflex to preserve frame pacing.
- **RTX 20 / 30 Series (Turing / Ampere)**: Integrated loader support for SM86 / SM75 modules (`dlssg_sm86`).

---

## Requirements

- **GPU**: NVIDIA RTX 20, 30, 40, or 50 series GPU.
  - Ada MFG Unlock: RTX 40 series.
  - Ampere/Turing MFG: RTX 20 / 30 series.
  - DLSS-NR: RTX 20, 30, 40, or 50 series.
- **OS**: Windows 10/11 64-bit or Linux via Proton/Wine.
- **Game**: DirectX 12 or Vulkan title supporting DLSS / Streamline (e.g., Cyberpunk 2077).
- **DLSS-NR Runtime**: `nvngx_dlssnr.dll` placed in the game executable directory (original NVIDIA version for RTX 50, or cross-generation compatibility runtime for RTX 20/30/40; see [INSTALL-DLSSNR.md](INSTALL-DLSSNR.md)).

---

## Installation on Windows

1. Close the game and back up any existing mod files.
2. Download `OptiScaler_v0.7.7-pre0_20260915.7z` from [Releases](https://github.com/Rakhna/optiscaler-mfg-nr/releases/tag/nightly).
3. Extract all files directly into the directory containing the game executable (e.g. `Cyberpunk 2077\bin\x64\`):
   - `OptiScaler.dll` (rename to `dxgi.dll` or run `setup_windows.bat`)
   - `nvngx.dll_dlssnr.dll`
   - `OptiScaler.ini`
   - `OptiScaler/` folder (FidelityFX and XeSS companion libraries)
4. Place the required `nvngx_dlssnr.dll` runtime alongside the executable.
5. Configure `OptiScaler.ini`:
   - For RTX 40 MFG: set `AdaMfgUnlock=true` under `[DLSSG]`.
   - For DLSS-NR: configure options under `[DlssNr]`.
6. Start the game, enable DLSS and Frame Generation in settings, and press **Insert** to toggle the in-game OptiScaler HUD.

> [!NOTE]
> **Upcoming Feature**: We will soon add a streamlined installer and uninstaller tool to make deploying, updating, and removing OptiScaler even easier across games.

---

## Keybindings & Menu Shortcut Configuration

By default, the OptiScaler in-game overlay menu is bound to the **Insert** key (`0x2D`). If your keyboard lacks an `Insert` key (common on 60%/75% compact or laptop keyboards), you can change it in `OptiScaler.ini` under the `[Menu]` section using hexadecimal Windows Virtual-Key codes:

```ini
[Menu]
; Shortcut key for opening the overlay menu:
; 0x2D = Insert (default)
; 0x7A = F11
; 0x7B = F12
; 0x24 = Home
; 0x08 = Backspace
; -1   = Disabled
ShortcutKey = 0x7A

; Optional shortcut toggles:
FpsShortcutKey      = 0x21 ; Page Up (toggles FPS overlay)
FpsCycleShortcutKey = 0x22 ; Page Down (cycles FPS overlay layout)
FGShortcutKey       = 0x23 ; End (toggles Frame Generation on/off)
```

Shortcuts can also be rebound interactively inside the running game from the **Keybinds** tab in the overlay menu.

---

## Important Notes

- Neural Rendering and high Multi-Frame Generation multipliers increase GPU memory and processing overhead.
- In-memory patching does not modify signed DLLs on disk.
- Avoid using in anti-cheat-protected online multiplayer games.

---

## Credits

- Based on [OptiScaler](https://github.com/optiscaler/OptiScaler) by optiscaler.
- Neural Rendering based on [OptiScaler_DLSSNR](https://github.com/Dagherbou/OptiScaler_DLSSNR) and [OptiScaler-DLSSNR-PreSR-Multipass](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass) with colour processing from [RenoDX](https://github.com/clshortfuse/renodx).
- RTX 40 Multi-Frame Generation memory patch concepts based on research by y4my4my4m.
- Ampere/Turing SM86 MFG loader integration based on sdli1995.

[Full credits](docs/CREDITS.md) · [Licence](LICENSE)
