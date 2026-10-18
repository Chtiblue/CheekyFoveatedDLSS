# Cheeky NR bridge preview 2 — menus and gameplay benchmark

Upgrade packages for **existing working installations**, based on Chtiblue's
triple-DLSS5 branch with this fork's shared-gaze compatibility. Experimental
prerelease; not a fresh installer. Existing settings are preserved.

## New menus

- **NR network preset: Default / Preset A / Preset B**, separate from
  **NR appearance mode: Model A / Model B / Model C**, for every NR pass.
- Standalone and UEVR use mutually exclusive button rows and descriptions.
  Appearance choices are not speed or quality rankings.
- Version-qualified network guidance: on NR DLL 310.9.16, A requests the
  older/larger network, B the newer/faster network; Default delegates to the DLL.
  Older or patched DLLs may behave differently.
- Optional **RenoDX-style cubic (unclamped)** NR upsampling alongside bilinear
  and clamped cubic. May ring around edges; defaults remain unchanged.
- Includes bounded same-frame pipeline capture diagnostic controls.

## Normal-gameplay A/B benchmark — standalone only

Enable NR and select **NR automatic network A/B benchmark → Start after-upscaling
A/B benchmark**. Close the menu within five seconds, then play normally for
about two minutes. Gaze tracking remains active; no stationary gaze required.

Tests four balanced A/B pairs after upscaling, changing the network preset on
all enabled NR passes together. Appearance, working scales, and enabled pass
count stay unchanged. Settings restore automatically; reopening the menu cancels.
Native D3D12 required. No screenshot helper is needed.

Reports save to `CheekyFoveatedDLSS/nr-benchmarks/<run>/result.txt`, with JSON
snapshots. Reports show NR GPU-block time, pair variation and observed dimension
checks—not total frame time or generated FPS. Inconsistent runs are inconclusive.
Scene changes and GPU contention remain possible confounders.

One tested Cyberpunk two-pass run with NR DLL 310.9.16.0 measured approximately
**7.21 ms on A versus 4.12 ms on B: 42.9% less GPU NR-block time**, with all four
pairs agreeing (41.8–44.3%). This is workload-specific, not a 43% FPS claim or a
before-upscaling result. UEVR has the new menus, not this automatic benchmark.

## Installation

Close the game and back up the files being replaced.

- **Standalone ZIP:** extract alongside the game executable, merging the existing
  `CheekyFoveatedDLSS` folder. Replace Host and Runtime together. Open your existing
  Cheeky menu key (F11 in the tested Cyberpunk setup; upstream default F8).
- **UEVR ZIP:** extract into `%APPDATA%/UnrealVRMod/<game-executable-name>/`, replacing
  the plugin, its Runtime, and `scripts/cheeky_foveated_dlss.lua` together. Update an
  existing global installation in its original location instead; do not duplicate
  it. Open LuaLoader → Cheeky Foveated DLSS → DLSS-NR. Adapter requires UEVR plugin
  API 2.39.0 or compatible newer 2.x.

Keep existing INIs, loaders, NVIDIA DLLs, OpenXR registration, OFXR and VR port.
No layer swap or Native Stereo Fix is required. Do not install plugin and addon
together. Proprietary NVIDIA DLLs and the optional image-capture helper are not
included. Read `START-HERE.txt` for color-path choices and diagnostic limitations.

Rollback: close the game and restore all backed-up package files. No registry
cleanup is needed.

## Validation scope

Native NR contracts, UEVR ABI/settings, standalone D3D12 and Streamline fixtures,
Vulkan regression, overlay/controller tests, runtime transient-setting restoration,
and Lua menu tests. Real Cyberpunk gameplay benchmark completed successfully.
Mock-model tests do not establish image quality; latest UEVR headset validation
remains pending. This is not full RenoDX parity.
