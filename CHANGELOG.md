# Changelog

## 1.0.1 (2026-10-17)

- The release zip now uses standard `/` folder paths, so 7-Zip, `unzip` and non-Windows tools extract the `EnshroudedBaseCap-docs` folder correctly. 1.0.0's zip stored them with backslashes; Windows Explorer handled it fine. The add-on itself is unchanged.

## 1.0.0 (2026-10-17)

First release.

- Caps Enshrouded's real (pre-frame-generation) frame rate. Frame generation multiplies the capped rate, so a cap of 40 gives 80 fps at 2x and 120 at 3x.
- Loads as an add-on of the Enshrouded DLSS Frame Generation mod (EnshroudedVKFG) through its `Addons=` setting.
- F9 / F11 lower / raise the cap in steps of 5 while the game has focus, with a short on-screen readout. The setting is saved.
- `install_basecap.bat` / `uninstall_basecap.bat` edit `EnshroudedVKFG.ini` for you.
- Tested in game on an RTX 4070 Laptop GPU (Windows 11 24H2, NVIDIA driver 617.42, EnshroudedVKFG build of 2026-10-15) at 2x and 3x.
