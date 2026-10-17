# NR Bridge + RDRVR ViMain integration (experimental)

This branch starts from satyaloka93's PR #2 head (0fff5eb) and ports the opt-in stereo array-slice workaround from the Chtiblue RDRVR diagnostic branch. It does **not** modify RDRVR, the stable release, or PR #2.

## RDR1

Place `CheekyStereo.ini` alongside `CheekyFoveatedDLSSRuntime.dll` with `[Stereo] StereoArraySlices=1`. Restart the game after changing the setting. Keep RDRVR `ViMain=1`. If the file is missing or set to 0, array-slice selection remains disabled.

The workaround assigns the first two observed native NGX handles to slices 0 and 1 for *both* depth and motion, only when both inputs are Texture2D arrays of exactly two slices. It is a provisional mapping established by visual testing in RDR1, **not** a general eye identification method. No assignment is made to a third handle.

The NR guide pass creates one-slice Texture2DArray SRVs when the source is an array. It preserves ordinary Texture2D SRVs for single-slice resources. Guide validation rejects multi-slice inputs when a valid slice is not explicitly supplied. The per-view GPU cache checks both slice identities to avoid reusing stale guide descriptors.

## Other games

Do not deploy `CheekyStereo.ini` with `StereoArraySlices=1` to CP77 or unrelated games unless intentionally testing this experimental mapping. The NR Bridge features themselves remain independently opt-in.

## Validation

Source integration only; no Windows Release build or headset test has been performed for this combined branch. Back up your known-good DLL, build, and test RDR1 before replacing other installations. Water reflections that move with the headset were also observed with DLSS5 disabled and are not addressed here.
