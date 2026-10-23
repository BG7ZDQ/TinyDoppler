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
2. The standalone app starts with no satellites or orbit sources. Use
   **Satellites and frequencies** to add a name, NORAD number, optional
   HTTP(S) TLE/GP/OMM link, and downlink frequencies, or download orbit data to
   discover satellites automatically. The NORAD field accepts decimal catalog
   numbers and standard five-character Alpha-5 values such as `A0465`
   (= 100465). Any HTTP(S) TLE or OMM JSON URL is accepted, not just CelesTrak.
   If the TLE/GP link is left blank, a CelesTrak `gp.php` link for that NORAD is
   generated. If both NORAD and a link contain `CATNR`, the two values must
   match. The link can be changed later in the same dialog.
   Saving a newly added satellite immediately saves its source and starts a
   background download, even if a cached orbit already exists.
   A failed download shows a warning with the source and error; the satellite
   and source remain available so **Update orbit data** can retry.
3. Use **Update orbit data** to refresh the configured sources; the application
   shows an error dialog if an orbit source cannot be parsed. A previously
   valid local cache remains available.

Satellite names, NORAD IDs, frequencies, and each satellite's selected
frequency are stored in `satellites.json`. The standalone catalog starts empty.
Downloads add newly discovered satellites by NORAD number without changing
existing names or frequency settings. New satellites have no frequency until
the user supplies one. Writes are atomic and keep the previous file as
`satellites.json.bak`.
Orbit data is cached per source; an unavailable source does not discard its
previously downloaded data when other sources update successfully.

On Windows the receiver keeps `satellites.json`, `tracker.ini` and
`ephemerides.json` in the installed main directory, beside `decoder/`,
`doppler/` and `launcher/`; a standalone or portable build uses the executable
directory. On Linux and macOS the application uses Qt's
`QStandardPaths::AppConfigLocation`, because the executable directory is usually
read-only or an ephemeral AppImage mount. `TINY_DOPPLER_CONFIG_DIR` can override
the location with an absolute path.

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
ASRTU receiver. The bridge and automatic Doppler tuning are enabled by default;
the panel starts in English and offers an English/中文/日本語 selector.

The bridge protocols are documented in [docs/INTERFACES.md](docs/INTERFACES.md).
Third-party notices are in [THIRD_PARTY.md](THIRD_PARTY.md).

## Physical radio control

The **电台控频… / Radio frequency…** dialog optionally connects to a Hamlib
`rigctld` already configured for the receiver model and serial port. The default
radio endpoint is `127.0.0.1:4534`. Only `F <corrected receive frequency in Hz>`
is sent on valid tracking ticks. SDR# and SDR++ are controlled through the
shared-memory record, not through this endpoint. There are no extra start/stop,
acknowledgement or VFO controls. Select the intended receive VFO on the radio
and run rigctld without explicit VFO mode. The feature defaults to disabled.

Example for IC-705 (verify model IDs with the installed `rigctl -l`):
`rigctld -m 3085 -r COM4 -s 19200 -T 127.0.0.1 -t 4534`.
IC-9700 uses model 3081; substitute the actual serial port and baud rate.
Linux ports typically use `/dev/ttyUSB*`; macOS uses `/dev/cu.*`.
See the [Hamlib protocol manual](https://hamlib.sourceforge.net/html/rigctld.1.html).
