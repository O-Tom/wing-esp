# ESP-IDF component wrapper for U8g2 clib.
#
# Overrides the upstream CMakeLists.txt which has REQUIRES "arduino" and
# therefore cannot be used with a pure ESP-IDF project.
#
# Source files are downloaded under .pio/libdeps by PlatformIO's lib_deps.
# This component is picked up first (project components/ beats EXTRA_COMPONENT_DIRS)
# so the broken upstream CMakeLists.txt is never used.

set(U8G2_CLIB "${PROJECT_DIR}/.pio/libdeps/esp32-s3-devkitc-1/U8g2/src/clib")

idf_component_register(
    SRC_DIRS     "${U8G2_CLIB}"
    INCLUDE_DIRS "${U8G2_CLIB}"
)
