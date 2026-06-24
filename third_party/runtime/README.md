# Third-party runtime layout

This directory is the project-relative home for non-Qt runtime libraries.

Expected local layout:

```text
third_party/runtime/
  ffmpeg/
    bin/*.dll
    include/libav*
    LICENSE.txt
  imagemagick/
    CORE_RL_MagickWand_.dll
    CORE_RL_MagickCore_.dll
    CORE_RL_*.dll
    *.xml
    modules/
```

The binary payload is intentionally ignored by Git. Put the same relative
layout here on another machine before configuring a package build.
