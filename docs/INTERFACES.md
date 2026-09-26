# Local bridge interfaces (version 1)

The mappings are Windows named shared memory in the current logon session.
Both are little-endian. They are local process interfaces, not network APIs.
The names and byte layouts are kept stable for existing ASRTU receivers and
SDR# plugin builds; a breaking layout change requires a new name and version.

## Doppler control: tracker to SDR# plugin

Mapping name: `Local\ASRTU_DOPPLER_CONTROL_V1`; length: 64 bytes. The tracker
owns the mapping and refreshes it every 500 ms. The plugin checks it every
500 ms and tunes only when `valid` is nonzero and the timestamp is no more
than 3 seconds old.

| Offset | Type | Meaning |
|---:|---|---|
| 0 | uint32 | Magic `0x504F4441` |
| 4 | uint32 | Version `1` |
| 8 | int64 | Target SDR# frequency in Hz |
| 16 | int64 | Doppler correction in Hz |
| 24 | int64 | Unix time in milliseconds when published |
| 32 | int32 | Valid flag (`0` or `1`) |
| 36–63 | bytes | Reserved, currently zero |

The target is the selected nominal downlink frequency plus the Doppler
correction. The tracker writes `valid=0` when there is no chosen frequency,
no matching orbit, or propagation fails. The plugin must not tune on an
absent, invalid, stale, or unknown-version mapping. On tracker shutdown the
mapping is marked invalid before it is released.

## Complex I/Q: SDR# plugin to receiver

Mapping name: `Local\ASRTU_IQ_BRIDGE_V1`. The plugin owns a ring of 262,144
complex samples. The 64-byte header is followed by interleaved float32
`I,Q` pairs (8 bytes per complex sample), at 48,000 samples/s. The plugin
resamples SDR#'s filtered I/Q when its input sample rate differs.

| Offset | Type | Meaning |
|---:|---|---|
| 0 | uint32 | Magic `0x42514941` |
| 4 | uint32 | Version `1` |
| 8 | float64 | Output sample rate, `48000.0` |
| 16 | uint32 | Ring capacity in complex samples, `262144` |
| 20 | uint32 | Bytes per complex sample, `8` |
| 24 | uint64 | Monotonic write index |
| 32 | float64 | Current SDR# input sample rate |
| 40 | int32 | Bridge enabled flag |
| 48 | int64 | Producer monotonic timer ticks |
| 56 | int32 | Producer process ID |
| 64 | float32[2] | First complex sample; the ring continues to the end |

The producer writes sample data before publishing the write index with a
release barrier. Consumers validate the header, read the index with acquire
semantics, and use `index % capacity` to locate samples. Consumers should
detect stale producers and may skip old samples to bound latency. The
mapping size is `64 + 262144 * 8` bytes.

## Catalog and orbit data

The user-editable `satellites.json` is schema version 1. Each entry contains a
numeric `norad`, display `name`, `frequenciesHz` array of integer hertz, and
`selectedHz` (zero if no frequency is selected). NORAD numbers in the JSON
are canonical decimal values. TLE Alpha-5 fields are decoded before matching
entries; for example `A0465` maps to `100465`. Invalid Alpha-5 letters `I`
and `O` are rejected. All user changes are saved via an atomic replacement.

The orbit-data cache is separate and may contain TLE and CelesTrak GP/OMM
JSON source bodies. A source with malformed or unusable elements produces a
visible error. If multiple sources include the same NORAD object, the newest
valid epoch is used.
