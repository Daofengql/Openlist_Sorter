cmake_minimum_required(VERSION 3.16)

project(openlist_sorter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(OPENLIST_SORTER_QT_ROOT "" CACHE PATH "Optional Qt5 installation prefix")
set(OPENLIST_SORTER_OPENSSL_ROOT "" CACHE PATH "Optional OpenSSL runtime prefix or bin directory")
set(OPENLIST_SORTER_RUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/runtime" CACHE PATH "Project-relative third-party runtime root")
set(OPENLIST_SORTER_IMAGECODECS_RUNTIME_DIR "${OPENLIST_SORTER_RUNTIME_ROOT}/imagecodecs" CACHE PATH "Direct image codec DLL runtime directory")
set(OPENLIST_SORTER_DIST_DIR "${CMAKE_BINARY_DIR}/dist" CACHE PATH "Directory for distributable output")

if(OPENLIST_SORTER_QT_ROOT)
  list(PREPEND CMAKE_PREFIX_PATH "${OPENLIST_SORTER_QT_ROOT}")
endif()

find_package(Qt5 REQUIRED COMPONENTS Widgets Network Multimedia MultimediaWidgets Svg Concurrent)

add_executable(openlist_sorter
  src/app/main.cpp
  src/api/openlist_client.cpp
  src/api/openlist_client.h
  src/core/connection_profile.cpp
  src/core/connection_profile.h
  src/core/models.h
  src/core/runtime_paths.cpp
  src/core/runtime_paths.h
  src/preview/heif_image_decoder.cpp
  src/preview/heif_image_decoder.h
  src/preview/image_converter.cpp
  src/preview/image_converter.h
  src/preview/image_decoder.cpp
  src/preview/image_decoder.h
  src/preview/preview_cache.cpp
  src/preview/preview_cache.h
  src/preview/webp_image_encoder.cpp
  src/preview/webp_image_encoder.h
  src/preview/wic_image_decoder.cpp
  src/preview/wic_image_decoder.h
  src/ui/connection_dialog.cpp
  src/ui/connection_dialog.h
  src/ui/connection_info_dialog.cpp
  src/ui/connection_info_dialog.h
  src/ui/main_window.cpp
  src/ui/main_window.h
  src/ui/remote_directory_dialog.cpp
  src/ui/remote_directory_dialog.h
  src/ui/zoomable_image_label.cpp
  src/ui/zoomable_image_label.h
  src/resources/resources.qrc
)

if(WIN32)
  enable_language(RC)
  target_sources(openlist_sorter PRIVATE src/resources/app.rc)
endif()

set_target_properties(openlist_sorter PROPERTIES
  OUTPUT_NAME "OpenListSorter"
  RUNTIME_OUTPUT_DIRECTORY "${OPENLIST_SORTER_DIST_DIR}"
  LIBRARY_OUTPUT_DIRECTORY "${OPENLIST_SORTER_DIST_DIR}"
)

foreach(config_type IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER "${config_type}" config_type_upper)
  set_target_properties(openlist_sorter PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_${config_type_upper} "${OPENLIST_SORTER_DIST_DIR}"
    LIBRARY_OUTPUT_DIRECTORY_${config_type_upper} "${OPENLIST_SORTER_DIST_DIR}"
  )
endforeach()

target_include_directories(openlist_sorter PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(openlist_sorter PRIVATE QT_NO_KEYWORDS)

target_link_libraries(openlist_sorter PRIVATE
  Qt5::Widgets
  Qt5::Network
  Qt5::Multimedia
  Qt5::MultimediaWidgets
  Qt5::Svg
  Qt5::Concurrent
)

if(WIN32)
  target_link_libraries(openlist_sorter PRIVATE ole32 windowscodecs uuid)
endif()

set(OPENLIST_SORTER_QT_CONF "${CMAKE_CURRENT_BINARY_DIR}/qt.conf")
file(WRITE "${OPENLIST_SORTER_QT_CONF}" "[Paths]\nPlugins = .\n")
add_custom_command(TARGET openlist_sorter POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${OPENLIST_SORTER_QT_CONF}"
          "$<TARGET_FILE_DIR:openlist_sorter>/qt.conf"
  COMMENT "Writing Qt plugin search path"
  VERBATIM
)

if(WIN32)
  set_target_properties(openlist_sorter PROPERTIES WIN32_EXECUTABLE TRUE)

  find_program(OPENLIST_SORTER_WINDEPLOYQT_EXECUTABLE
    NAMES windeployqt windeployqt-qt5
    HINTS "${OPENLIST_SORTER_QT_ROOT}/bin"
    DOC "Qt runtime deployment tool"
  )

  if(OPENLIST_SORTER_WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET openlist_sorter POST_BUILD
      COMMAND "${OPENLIST_SORTER_WINDEPLOYQT_EXECUTABLE}"
              --release
              --no-translations
              --no-angle
              --no-opengl-sw
              --no-system-d3d-compiler
              --no-compiler-runtime
              "$<TARGET_FILE:openlist_sorter>"
      COMMENT "Deploying Qt runtime DLLs into dist"
      VERBATIM
    )
  else()
    message(WARNING "windeployqt was not found; Qt DLLs will not be deployed automatically.")
  endif()

  get_filename_component(OPENLIST_SORTER_COMPILER_BIN_DIR
    "${CMAKE_CXX_COMPILER}" DIRECTORY)
  foreach(runtime_dll IN ITEMS
      libstdc++-6.dll
      libgcc_s_seh-1.dll
      libwinpthread-1.dll)
    if(EXISTS "${OPENLIST_SORTER_COMPILER_BIN_DIR}/${runtime_dll}")
      add_custom_command(TARGET openlist_sorter POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${OPENLIST_SORTER_COMPILER_BIN_DIR}/${runtime_dll}"
                "$<TARGET_FILE_DIR:openlist_sorter>/${runtime_dll}"
        COMMENT "Deploying ${runtime_dll}"
        VERBATIM
      )
    endif()
  endforeach()

  foreach(qt_runtime_dll IN ITEMS Qt5Concurrent.dll)
    if(EXISTS "${OPENLIST_SORTER_QT_ROOT}/bin/${qt_runtime_dll}")
      add_custom_command(TARGET openlist_sorter POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${OPENLIST_SORTER_QT_ROOT}/bin/${qt_runtime_dll}"
                "$<TARGET_FILE_DIR:openlist_sorter>/${qt_runtime_dll}"
        COMMENT "Deploying ${qt_runtime_dll}"
        VERBATIM
      )
    endif()
  endforeach()

  set(OPENLIST_SORTER_OPENSSL_HINTS
    "${OPENLIST_SORTER_OPENSSL_ROOT}"
    "${OPENLIST_SORTER_OPENSSL_ROOT}/bin"
    "D:/env/.espressif/tools/idf-git/2.39.2/mingw64/bin"
    "D:/env/OpenSSL-Win64/bin"
    "D:/env/OpenSSL-Win64"
  )
  find_file(OPENLIST_SORTER_OPENSSL_SSL_DLL
    NAMES libssl-1_1-x64.dll libssl-1_1.dll libssl-3-x64.dll libssl-3.dll
    HINTS ${OPENLIST_SORTER_OPENSSL_HINTS}
    DOC "OpenSSL SSL runtime DLL for Qt Network"
  )
  find_file(OPENLIST_SORTER_OPENSSL_CRYPTO_DLL
    NAMES libcrypto-1_1-x64.dll libcrypto-1_1.dll libcrypto-3-x64.dll libcrypto-3.dll
    HINTS ${OPENLIST_SORTER_OPENSSL_HINTS}
    DOC "OpenSSL crypto runtime DLL for Qt Network"
  )
  if(OPENLIST_SORTER_OPENSSL_SSL_DLL AND OPENLIST_SORTER_OPENSSL_CRYPTO_DLL)
    add_custom_command(TARGET openlist_sorter POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              "${OPENLIST_SORTER_OPENSSL_SSL_DLL}"
              "$<TARGET_FILE_DIR:openlist_sorter>"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
              "${OPENLIST_SORTER_OPENSSL_CRYPTO_DLL}"
              "$<TARGET_FILE_DIR:openlist_sorter>"
      COMMENT "Deploying OpenSSL runtime DLLs for HTTPS"
      VERBATIM
    )
  else()
    message(WARNING "OpenSSL runtime DLLs were not found; HTTPS requests may fail with TLS initialization failed.")
  endif()

  add_custom_command(TARGET openlist_sorter POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -rf
            "$<TARGET_FILE_DIR:openlist_sorter>/cache"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf
            "$<TARGET_FILE_DIR:openlist_sorter>/runtime/imagecodecs"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf
            "$<TARGET_FILE_DIR:openlist_sorter>/runtime/imagemagick"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf
            "$<TARGET_FILE_DIR:openlist_sorter>/runtime/ffmpeg"
    COMMENT "Cleaning third-party runtime output"
    VERBATIM
  )

  if(EXISTS "${OPENLIST_SORTER_IMAGECODECS_RUNTIME_DIR}")
    add_custom_command(TARGET openlist_sorter POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory
              "$<TARGET_FILE_DIR:openlist_sorter>/runtime/imagecodecs"
      COMMENT "Deploying direct image codec runtime"
      VERBATIM
    )
    foreach(imagecodec_runtime_file IN ITEMS
        CORE_RL_brotli_.dll
        CORE_RL_heif_.dll
        CORE_RL_webp_.dll
        CORE_RL_zlib_.dll
        libheif.dll
        libheif-1.dll
        libwebp.dll
        libwebp-7.dll
        libsharpyuv.dll
        libsharpyuv-0.dll
        zlib1.dll)
      if(EXISTS "${OPENLIST_SORTER_IMAGECODECS_RUNTIME_DIR}/${imagecodec_runtime_file}")
        add_custom_command(TARGET openlist_sorter POST_BUILD
          COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                  "${OPENLIST_SORTER_IMAGECODECS_RUNTIME_DIR}/${imagecodec_runtime_file}"
                  "$<TARGET_FILE_DIR:openlist_sorter>/runtime/imagecodecs"
          COMMENT "Deploying image codec ${imagecodec_runtime_file}"
          VERBATIM
        )
      endif()
    endforeach()
  else()
    message(WARNING "Direct image codec runtime was not found under ${OPENLIST_SORTER_IMAGECODECS_RUNTIME_DIR}; HEIC decode and WebP conversion may be unavailable.")
  endif()

  foreach(unused_qt_imageformat IN ITEMS
      imageformats/qgif.dll
      imageformats/qgifd.dll
      imageformats/qicns.dll
      imageformats/qicnsd.dll
      imageformats/qico.dll
      imageformats/qicod.dll
      imageformats/qjp2.dll
      imageformats/qjp2d.dll
      imageformats/qjpeg.dll
      imageformats/qjpegd.dll
      imageformats/qmng.dll
      imageformats/qmngd.dll
      imageformats/qtga.dll
      imageformats/qtgad.dll
      imageformats/qtiff.dll
      imageformats/qtiffd.dll
      imageformats/qwbmp.dll
      imageformats/qwbmpd.dll
      imageformats/qwebp.dll
      imageformats/qwebpd.dll)
    add_custom_command(TARGET openlist_sorter POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E rm -f
              "$<TARGET_FILE_DIR:openlist_sorter>/${unused_qt_imageformat}"
      COMMENT "Removing unused Qt imageformat plugin ${unused_qt_imageformat}"
      VERBATIM
    )
  endforeach()

  foreach(stale_debug_runtime IN ITEMS
      Qt5Concurrentd.dll
      Qt5Cored.dll
      Qt5Guid.dll
      Qt5Multimediad.dll
      Qt5MultimediaWidgetsd.dll
      Qt5Networkd.dll
      Qt5OpenGLd.dll
      Qt5Svgd.dll
      Qt5Widgetsd.dll
      audio/qtaudio_windowsd.dll
      bearer/qgenericbearerd.dll
      iconengines/qsvgicond.dll
      imageformats/qsvgd.dll
      mediaservice/dsengined.dll
      mediaservice/qtmedia_audioengined.dll
      platforms/qwindowsd.dll
      playlistformats/qtmultimedia_m3ud.dll
      styles/qwindowsvistastyled.dll)
    add_custom_command(TARGET openlist_sorter POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E rm -f
              "$<TARGET_FILE_DIR:openlist_sorter>/${stale_debug_runtime}"
      COMMENT "Removing stale debug runtime ${stale_debug_runtime}"
      VERBATIM
    )
  endforeach()

  foreach(stale_tool IN ITEMS ffmpeg.exe ffplay.exe ffprobe.exe magick.exe)
    add_custom_command(TARGET openlist_sorter POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E rm -f
              "$<TARGET_FILE_DIR:openlist_sorter>/${stale_tool}"
      COMMENT "Removing stale external tool ${stale_tool}"
      VERBATIM
    )
  endforeach()
elseif(UNIX AND NOT APPLE)
  set_target_properties(openlist_sorter PROPERTIES
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
  )

  add_custom_command(TARGET openlist_sorter POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
            "-DOPENLIST_SORTER_DEPLOY_TARGET=$<TARGET_FILE:openlist_sorter>"
            "-DOPENLIST_SORTER_DEPLOY_DIR=$<TARGET_FILE_DIR:openlist_sorter>"
            "-DOPENLIST_SORTER_DEPLOY_QT_ROOT=${OPENLIST_SORTER_QT_ROOT}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/openlist_sorter_deploy_runtime_deps.cmake"
    COMMENT "Deploying shared libraries into dist"
    VERBATIM
  )
endif()
