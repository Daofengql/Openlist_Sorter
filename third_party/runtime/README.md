# Third-party runtime layout

This directory is the project-relative home for non-Qt runtime libraries.

Expected local layout:

```text
third_party/runtime/
  imagecodecs/
    # Windows runtime copied by CMake
    CORE_RL_heif_.dll
    CORE_RL_webp_.dll
    CORE_RL_brotli_.dll
    CORE_RL_zlib_.dll
```

The app no longer calls ImageMagick / MagickWand. Runtime image work is routed
to one implementation per format: WIC for common Windows raster formats,
libheif for HEIC / HEIF / AVIF, libwebp for WebP decode and encode, and Qt only
for SVG.

The binary payload is intentionally ignored by Git. Put the same relative
layout here on another Windows machine before configuring a package build.
VC++ runtime DLLs are intentionally not copied into the package; install the
Microsoft Visual C++ 2015-2022 x64 Redistributable on clean systems if the
codec DLLs fail to load.
