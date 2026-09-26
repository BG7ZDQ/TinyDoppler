# Third-party components

- `third_party/sgp4`: C99 SGP4 implementation by Andrew C. Young, MIT.
  The upstream copyright and complete license are retained in
  `third_party/sgp4/SGP4_LICENSE.txt`. Tiny Doppler adds Alpha-5 parsing,
  validation, and formatting to its local copy.
- Qt 5: linked dynamically under the license terms of the Qt distribution
  used to build and package the app.
- SDR# API assemblies: only referenced during optional plugin compilation;
  they are not included in this repository. SDR# and its components retain
  their respective authorship and terms.

Tiny Doppler's original application and plugin code is licensed under the
repository's MIT license.
