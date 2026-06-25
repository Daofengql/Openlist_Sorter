# Third-party runtime layout

This directory is the project-relative home for non-Qt runtime libraries.

Expected local layout:

```text
third_party/runtime/
  imagemagick/
    # Windows runtime copied by CMake
    CORE_RL_MagickWand_.dll
    CORE_RL_MagickCore_.dll
    CORE_RL_heif_.dll
    CORE_RL_*.dll
    *.xml
    modules/
    # Linux release package runtime collected by GitHub Actions
    lib/
      libMagickWand-*.so*
      libheif.so*
    config/
    modules/
```

The app calls MagickWand for general uncommon image conversion and directly
calls the libheif API for problematic HEIC / HEIF / AVIF previews. On Windows
libheif is provided by ImageMagick's `CORE_RL_heif_.dll`; on Linux release
packages it is collected from the system runtime libraries into
`runtime/imagemagick/lib`.

The binary payload is intentionally ignored by Git. Put the same relative
layout here on another Windows machine before configuring a package build.
