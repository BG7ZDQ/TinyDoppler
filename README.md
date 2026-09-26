# Tiny Doppler

Tiny Doppler is a small satellite tracker and SDR# bridge. The desktop app
computes downlink Doppler correction from TLE or CelesTrak GP/OMM JSON orbital
data. The optional SDR# plugin follows its output and can publish 48 ksample/s
complex I/Q to a local receiver. The app also works without SDR#; automatic
tuning and I/Q delivery require the Windows plugin.

## Use

1. Start `TinyDoppler` and use **Ground station** to enter observer
   coordinates. They are remembered locally. A launcher can also supply them
   with `--longitude`, `--latitude`, and `--altitude` (degrees, degrees,
   metres). See `--help` for all options.
2. Select a satellite and one of its downlink frequencies. Use **Manage
   satellites and frequencies** to add, rename, or remove satellites and
   frequencies. The NORAD field accepts decimal catalog numbers and standard
   five-character Alpha-5 values such as `A0465` (= 100465).
3. Use **Update orbit data** to refresh the configured sources. **Sources**
   edits their URLs. The application shows an error dialog if an orbit source
   cannot be parsed. A previously valid local cache remains available.

Satellite names, NORAD IDs, frequencies, and each satellite's selected
frequency are stored in the user configuration directory as `satellites.json`.
The file is created from `assets/default_satellites.json` on first run.
Updates merge only newly supplied default satellites; existing user entries
and frequency choices are preserved. Writes are atomic. Orbital data and the
satellite catalog are separate: downloading new TLE/OMM does not overwrite
operator frequency settings. Use **Open folder** in the app to find its
downloaded orbital-data cache.

The exact configuration path is the platform's Qt
`QStandardPaths::AppConfigLocation` for the `TinyDoppler` application. It is
not placed beside the executable, because that directory may be read-only.

## Build the app

Requires CMake 3.20+, a C/C++ compiler, and Qt 5.15 with Core, Gui, Widgets,
and Network. No GNU Radio, SDR#, installer generator, or main receiver tree is
required.

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

On Windows with Visual Studio Build Tools, use generator
`Visual Studio 17 2022` and `-A x64`. Runtime Qt DLL deployment can be done
with `windeployqt` for a portable directory. This repository intentionally
does not provide an Inno Setup installer.

## Build the SDR# plugin

The plugin targets the SDR# Studio v1920 API, .NET Framework 4.6, and x86.
The SDR# API DLLs are not part of this repository. Point the build script at
an existing compatible SDR# installation or API directory:

```powershell
.\plugin\build_legacy.ps1 -SdrSharpApiRoot 'C:\path\to\SDRSharp'
```

Copy the resulting `plugin/bin/Release/net46/SDRSharp.AstroSeriesBridge.dll`
to the SDR# `Plugins` directory. The plugin can be used independently of the
ASRTU receiver. Its automatic Doppler setting is saved per Windows user.

The bridge protocols are documented in [docs/INTERFACES.md](docs/INTERFACES.md).
Third-party notices are in [THIRD_PARTY.md](THIRD_PARTY.md).
