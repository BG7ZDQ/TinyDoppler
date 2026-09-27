# Tiny Doppler SDR# bridge

`SDRSharp.AstroSeriesBridge.dll` is an optional plugin for SDR# Studio v1920 (x86, .NET Framework 4.6). It is not required to run Tiny Doppler or the ASRTU receiver.

Copy the DLL into the compatible SDR# installation's `Plugins` directory, then restart SDR#. The panel appears as **Tiny Doppler**. Enable **Send I/Q to receiver** to publish SDR# RAW I/Q to the local receiver. Enable **Automatic Doppler tuning** only when desired; it follows the frequency calculated by the Tiny Doppler application. Automatic tuning is off by default.

The plugin does not contain SDR# or its API assemblies. Use a matching SDR# installation; other API versions are not guaranteed to work.
