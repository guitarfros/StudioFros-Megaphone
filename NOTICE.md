# Credits and notices

## StudioFros - Megaphone

Copyright (C) GuitarFros.
The source code is released under the **GNU Affero General Public License v3.0** (see `LICENSE`).

## Interface artwork

The images of the interface (`Design/panel_original.jpg`, and the pieces derived from it in `Assets/`
and `docs/`) were **generated with Meta AI** (https://www.meta.ai) and then adapted by GuitarFros for this
plugin: retouched (for example the MODE captions and the BYPASS plate), cut into pieces and animated by
the code (knob pointers, switch levers, LED, LCD text).

They are distributed free of charge together with the plugin so that anyone can use, share and rebuild it.
`tools/make_assets.py` rebuilds every piece in `Assets/` from `Design/panel_original.jpg`.

## Third-party software

- **JUCE** (https://juce.com) - audio plugin framework, used under the terms of the AGPLv3.
  JUCE is downloaded by CMake when the plugin is built and is not included in this repository.
- **VST** is a registered trademark of Steinberg Media Technologies GmbH.

`tools/make_assets.py` (a build helper, not part of the plugin) uses Python, NumPy and OpenCV.
