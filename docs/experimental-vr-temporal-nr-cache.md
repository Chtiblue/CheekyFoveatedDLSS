# Experimental VR temporal NR cache — feasibility audit

Status: **design / investigation only**. No rendering behavior has been changed.

Base: `feature/standalone-triple-dlss5`
Reference: https://github.com/Skynizz/optiscaler-dlss5/blob/main/OptiScaler/dlssnr/design/edit-cache.md

## Existing Cheeky infrastructure (verified)

- `src/dlss_nr.cpp` tracks NR histories and routes; extra passes have distinct private view-ID bits (`kNrPass2ViewBit`, `kNrPass3ViewBit`). Those bits are for temporal history, not proof of a reusable per-pass edit cache.
- `src/dlss_nr.cpp` validates non-null depth and motion-vector resources, nonzero guide dimensions, finite jitter and motion UV scales, and rejects unsupported 3D motion vectors in the inspected path.
- `src/crop_motion.cpp` contains D3D12 motion/depth cropping helpers.
- `src/dlss_nr_input.cpp` contains per-view input history compatibility and reset tracking.
- The current standalone Triple DLSS5 already operates NR pre-DLSS; pre-SR is **not** a proposed optimization here.

## Skynizz design (source-derived)

- Cache a *log ratio of the model's correction*, not an old rendered image.
- Reproject using motion vectors; validate using depth, color, and optional fingerprint/context.
- Distinguish low-frequency illumination from high-frequency synthesized detail.
- Adapt refresh cadence and invalidate on history discontinuities.
- Requires extra D3D12 resources and shader passes; copying its OptiScaler implementation directly into Cheeky is not safe.

## VR-specific design constraints (proposals, unverified)

1. **Isolation:** independently key histories by physical eye, NR pass, dimensions, processing route and any view/region identity needed. No cross-eye history reuse.
2. **Gaze changes:** when a peripheral pixel enters the foveal zone, prefer a fresh NR evaluation; avoid carrying stale peripheral detail into fixation.
3. **Per-eye synchronization:** compare frame pacing and inter-eye quality before staggering pass refreshes; staggering is experimental, not assumed beneficial.
4. **Motion/depth coordinates:** verify guide UV scales, crop origins, jitter conventions and depth formats against the Skynizz reprojection shader before porting.
5. **Reset conditions:** invalidate on eye/view reassignment, gaze discontinuity, recenter, resize, route/setting change, scene cut, or NGX history reset.
6. **Resource lifetime:** respect command-list submission/fence completion and avoid reuse while GPU work is pending.
7. **Fail-safe:** opt-in disabled by default, with exact fallback to existing triple-pass behavior when guides are invalid.

## Proposed implementation order

- Instrument GPU cost per physical eye and NR pass, including frame-time variance.
- Prototype a **peripheral-only** edit cache, with the foveal region refreshed every frame.
- Validate motion/depth/jitter reprojection on static, panning, occlusion and fast gaze-switch scenes.
- Add confidence/rejection visualization and conservative cache invalidation.
- Compare against baseline on RTX 5090 / Pimax Dream Air before considering a release.

## Acceptance criteria

- No regression with cache disabled.
- No history contamination between eyes or passes.
- No persistent trails on moving edges or newly foveated areas.
- Measured benefit in GPU cost **and** acceptable 1% lows / frame pacing.
- No merge to stable until reproducible headset testing.

## Licensing

Skynizz/optiscaler-dlss5 declares GPL-3.0. Before copying code, inspect Cheeky's applicable licenses and preserve attribution / compatible distribution terms. Prefer adapting ideas independently where appropriate.
