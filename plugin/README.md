# Tiny Doppler SDR# bridge

`SDRSharp.AstroSeriesBridge.dll` is an optional plugin for SDR# Studio v1920 (x86, .NET Framework 4.6). It is not required to run Tiny Doppler or the ASRTU receiver.

Copy the DLL into the compatible SDR# installation's `Plugins` directory, then restart SDR#. The panel appears as **Tiny Doppler**. **Enable shared-memory bridge** and **Automatic Doppler tuning** are enabled by default; the bridge publishes SDR# RAW I/Q and the tuning follows the frequency calculated by the Tiny Doppler application. The panel starts in English and has an English/中文/日本語 language selector.

The plugin does not contain SDR# or its API assemblies. Use a matching SDR# installation; other API versions are not guaranteed to work.

## Shared bridge ownership

The updated SDR# and SDR++ ASRTU plugins share the named lifetime token
`Local\ASRTU_BRIDGE_OWNER_V1`. A second active bridge is refused before it
can change the I/Q mapping or tune. Uncheck **Enable shared-memory bridge**
to release ownership and stop both I/Q and Doppler, then retry in the other
application. Normal exit and process crashes also release the token.
Update both plugins: older binaries do not honor this ownership protocol.
TinyDoppler publishes a V1-compatible sequence at control offset 36 so readers
reject incomplete multi-field updates as well as stale commands.
