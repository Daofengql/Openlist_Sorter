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
set(OPENLIST_SORTER_DIST_DIR "${CMAKE_BINARY_DIR}/dist" CACHE PATH "Directory for distributable output")

if(OPENLIST_SORTER_QT_ROOT)
  list(PREPEND CMAKE_PREFIX_PATH "${OPENLIST_SORTER_QT_ROOT}")
endif()

find_package(Qt5 REQUIRED COMPONENTS Widgets Network Multimedia MultimediaWidgets Svg)

add_executable(openlist_sorter
  src/app/main.cpp
  src/api/openlist_client.cpp
  src/api/openlist_client.h
  src/core/connection_profile.cpp
  src/core/connection_profile.h
  src/core/models.h
  src/preview/image_decoder.cpp
  src/preview/image_decoder.h
  src/ui/connection_dialog.cpp
  src/ui/connection_dialog.h
  src/ui/connection_info_dialog.cpp
  src/ui/connection_info_dialog.h
  src/ui/main_window.cpp
  src/ui/main_window.h
  src/ui/remote_directory_dialog.cpp
  src/ui/remote_directory_dialog.h
)

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
)

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
    NAMES windeployqt
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
