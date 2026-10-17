# NR pipeline diagnostics

This opt-in D3D12 diagnostic records intermediate pixels, not a final headset
screenshot. It changes no tuning and retains the selected before/after-upscaling
order. It is not a performance benchmark.

## Controls

- Standalone: NR same-frame pipeline capture -> Capture NR pipeline (10 seconds).
- UEVR: Cheeky Foveated DLSS -> DLSS-NR (experimental) -> NR pipeline capture
  (diagnostic) -> Capture NR pipeline (10 seconds).

Enable NR first. Close menus within ten seconds and remain in gameplay for about
forty seconds. Status appears beside the controls. UEVR requires matching new
runtime and Lua files; older runtimes hide the unsupported menu. The separate
seven-stage mirror comparison remains standalone-only and requires its external
capture helper; this raw diagnostic does not require that helper.

## Output and interpretation

Under the runtime configuration directory:
`nr-pipeline-captures/<pid>-<tick>/view-<id>-pass-<n>/` contains:

- `original.bin`: input before this NR pass.
- `proxy.bin`: encoded/resampled input presented to the model.
- `neural.bin`: model output before restoration/blending.
- `restored.bin`: output after this pass, before the next pass.
- `exposure.bin`: accepted game exposure texture, if present.
- `codec-constants.bin`: actual 192-byte restoration shader constants.
- `capture.json`: formats, pitches, sizes, processing order, tuning and exposure
  availability/validation reason.

Wait for saving to finish before analysis. Raw files are padded native textures,
not PNGs. Install NumPy and run:

```sh
python scripts/analyze_nr_pipeline.py path/to/capture --output report.json
```

Two distinct successful base-view chains are sampled, up to three passes each.
Views are not guaranteed simultaneous and are not assumed to be left/right eyes.
All planes within a pass belong to that invocation. In before-upscaling mode,
subsequent SR can change the result; restored pixels are not final SR output.
Raw differences establish activity, not visual superiority or reference parity.

Copies use tracked resource states and wait for recording retirement and GPU
completion before mapping. Limits: 32 MiB per plane, 64 MiB per job, six jobs.
Unknown completion retains resources rather than freeing in-flight memory.
An explicitly requested capture pins the code module for background writer
lifetime; this diagnostic can therefore prevent unloading it until process exit.

## Validation scope

Synthetic nonidentity-model tests exercise two-pass readback, exact chaining,
and an accepted exposure texture with value 2. Real Cyberpunk VR captures on
2026-10-17 confirmed both passes modify pixels and pass-1 output exactly matches
pass-2 input, with before and after upscaling tested separately. Those captures
had successful but null game-exposure queries with auto-exposure enabled.

UEVR menu behavior is tested with LuaJIT 2.1 and Lua 5.4 mocked hosts; a live
UEVR headset capture with the new button has not been validated. No complete
RenoDX parity, final-image quality improvement, or speedup is claimed. Exact
filter equivalence, temporal guides and additional gamut paths remain open.
