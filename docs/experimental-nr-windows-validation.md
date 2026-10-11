# Windows validation — experimental NR GPU timestamps

The experimental branch has **not yet been verified by a successful Windows build**.

## Build locally (Visual Studio 2022 + CMake)

From a Windows Developer PowerShell:

```powershell
git clone --recurse-submodules https://github.com/Chtiblue/CheekyFoveatedDLSS.git
cd CheekyFoveatedDLSS
git switch experiment/nr-temporal-cache-vr
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCHEEKY_BUILD_UEVR=OFF -DCHEEKY_BUILD_STANDALONE=ON
cmake --build build --config Release --target CheekyFoveatedDLSSRuntime --parallel 2
cmake --build build --config Release --target CheekyNrObserverTests --parallel 2
ctest --test-dir build -C Release --output-on-failure -R CheekyNrObserver
```

An equivalent experimental GitHub Actions workflow is stored at
`.github/workflows/experimental-nr-timing.yml`. No successful run has
yet been observed; check repository Actions permissions, workflow visibility
and the Actions tab before assuming CI is active.

## Runtime diagnostic (only after successful build)

The instrumentation is disabled unless the process is launched with
`CHEEKY_NR_GPU_TIMING=1`. It surrounds the NGX `evaluate_feature` call,
**not** all preprocessing/codec work of a full NR pass.

Expected logs:
- `[NR-CPU-SUBMIT]` — CPU wall time around NGX submission.
- `[NR-GPU-TICKS]` — GPU timestamp difference in raw ticks; **not ms**.

Important validation:
1. Run with the environment variable unset first; compare against stable behavior.
2. Enable only in a controlled diagnostic session, never in a release build.
3. Confirm no D3D12 debug-layer warnings (timestamp query, resolve, resource lifetime).
4. Verify samples are eventually retired after command-list reset and queue completion.
5. Inspect stereo behavior and frame pacing. Disable on any crash or rendering artifact.

## Outstanding correctness and measurement tasks

- Associate samples with their actual executing queue and timestamp frequency;
  do not label raw ticks as milliseconds.
- Ensure pending samples that were recorded but never executed are not treated
  as valid measurements. A completed fence and a valid queue association are
  needed before trusting a timestamp.
- Verify the sample resource lifetime under repeated command-list execution,
  device loss, multi-queue submission, and command-list reset.
- Measure encode/decode/guide preparation separately or explicitly label
  NGX-only timings.
- Add a reproducible GPU test on real D3D12 hardware.

Do **not** merge or publish a test DLL until the above build and safety checks pass.
