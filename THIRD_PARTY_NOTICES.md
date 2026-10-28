# Third-party notices

This project contains material derived from **tuya-local**. Those portions are
licensed under the MIT License below. The rest of this repository remains under
the Unlicense; see [`LICENSE`](LICENSE).

## tuya-local

- Upstream: <https://github.com/make-all/tuya-local>
- License: MIT
- Material used:
  - tuya-local-style device descriptors and profile metadata;
  - the profile/entity mapping behaviour implemented in
    `web/profile_mapping.mjs` and `web/profile_converter.mjs`;
  - generated catalog and firmware profile assets derived from the above,
    including `web/catalog.json` and generated files in `firmware/`.

The catalog generator reads a local tuya-local checkout; the browser fetches
descriptor YAML from the upstream repository. No tuya-local code is vendored as
a git submodule, but the derived source and generated files above remain subject
to the upstream MIT terms.

```text
Copyright 2018 Nik Rolls, 2026 Jason Rumney, 2022-2026 other contributors as listed in ACKNOWLEDGEMENTS.md

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

The upstream copyright line in full is:

```text
Copyright 2018 Nik Rolls, 2026 Jason Rumney, 2022-2026 other contributors as listed in ACKNOWLEDGEMENTS.md
```

If a generated profile pack or binary is distributed separately, include this
notice file (or the MIT text above) with it.
