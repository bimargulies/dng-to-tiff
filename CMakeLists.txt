cmake_minimum_required(VERSION 3.20)

# Force Apple Clang — the DNG SDK has clang-specific code paths and
# the dependency Xcode projects were built with clang.
if(APPLE AND NOT CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER   /usr/bin/clang   CACHE STRING "" FORCE)
    set(CMAKE_CXX_COMPILER /usr/bin/clang++ CACHE STRING "" FORCE)
endif()

project(dng_to_tiff CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # used by scripts/lint.sh and editors

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

set(SDK_ROOT "${CMAKE_SOURCE_DIR}/third-party/dng_sdk")
set(DNG_SRC  "${SDK_ROOT}/dng_sdk/source")
set(JPEG_SRC "${SDK_ROOT}/libjpeg")
set(JXL_SRC  "${SDK_ROOT}/libjxl/libjxl")
set(JXL_CLIENT_INC "${SDK_ROOT}/libjxl/client_projects/include")

# XMP static libs are built by scripts/build-deps.sh
set(XMP_LIB_DIR "${SDK_ROOT}/xmp/toolkit/public/libraries/macintosh/intel_64_libcpp/Release")
set(XMP_INC     "${SDK_ROOT}/xmp/toolkit/public/include")

# libjxl static lib is built by scripts/build-deps.sh
set(JXL_LIB_DIR "${SDK_ROOT}/libjxl/client_projects/mac/build")

# ---------------------------------------------------------------------------
# Check that pre-built deps exist
# ---------------------------------------------------------------------------

foreach(lib
    "${XMP_LIB_DIR}/libXMPCoreStaticRelease.a"
    "${XMP_LIB_DIR}/libXMPFilesStaticRelease.a"
    "${JXL_LIB_DIR}/libjxl_release.a")
  if(NOT EXISTS "${lib}")
    message(FATAL_ERROR
      "Missing pre-built dependency: ${lib}\n"
      "Run scripts/build-deps.sh first.")
  endif()
endforeach()

# ---------------------------------------------------------------------------
# libjpeg (compile from SDK source; only the files needed by the DNG SDK)
# ---------------------------------------------------------------------------

set(LIBJPEG_SOURCES
    ${JPEG_SRC}/jaricom.c
    ${JPEG_SRC}/jcapimin.c  ${JPEG_SRC}/jcapistd.c  ${JPEG_SRC}/jcarith.c
    ${JPEG_SRC}/jccoefct.c  ${JPEG_SRC}/jccolor.c    ${JPEG_SRC}/jcdctmgr.c
    ${JPEG_SRC}/jchuff.c    ${JPEG_SRC}/jcinit.c     ${JPEG_SRC}/jcmainct.c
    ${JPEG_SRC}/jcmarker.c  ${JPEG_SRC}/jcmaster.c   ${JPEG_SRC}/jcomapi.c
    ${JPEG_SRC}/jcparam.c   ${JPEG_SRC}/jcprepct.c   ${JPEG_SRC}/jcsample.c
    ${JPEG_SRC}/jctrans.c   ${JPEG_SRC}/jdapimin.c   ${JPEG_SRC}/jdapistd.c
    ${JPEG_SRC}/jdarith.c   ${JPEG_SRC}/jdatadst.c   ${JPEG_SRC}/jdatasrc.c
    ${JPEG_SRC}/jdcoefct.c  ${JPEG_SRC}/jdcolor.c    ${JPEG_SRC}/jddctmgr.c
    ${JPEG_SRC}/jdhuff.c    ${JPEG_SRC}/jdinput.c    ${JPEG_SRC}/jdmainct.c
    ${JPEG_SRC}/jdmarker.c  ${JPEG_SRC}/jdmaster.c   ${JPEG_SRC}/jdmerge.c
    ${JPEG_SRC}/jdpostct.c  ${JPEG_SRC}/jdsample.c   ${JPEG_SRC}/jdtrans.c
    ${JPEG_SRC}/jerror.c    ${JPEG_SRC}/jfdctflt.c   ${JPEG_SRC}/jfdctfst.c
    ${JPEG_SRC}/jfdctint.c  ${JPEG_SRC}/jidctflt.c   ${JPEG_SRC}/jidctfst.c
    ${JPEG_SRC}/jidctint.c  ${JPEG_SRC}/jmemmgr.c    ${JPEG_SRC}/jmemnobs.c
    ${JPEG_SRC}/jquant1.c   ${JPEG_SRC}/jquant2.c    ${JPEG_SRC}/jutils.c
)

add_library(libjpeg_sdk STATIC ${LIBJPEG_SOURCES})
target_include_directories(libjpeg_sdk PUBLIC ${JPEG_SRC})
target_compile_options(libjpeg_sdk PRIVATE -w)  # suppress jpeg's own warnings

# ---------------------------------------------------------------------------
# DNG SDK library
# ---------------------------------------------------------------------------

# All .cpp files except dng_validate.cpp (has its own main) and dng_xmp* (need XMP)
set(DNG_SOURCES
    ${DNG_SRC}/dng_1d_function.cpp
    ${DNG_SRC}/dng_1d_table.cpp
    ${DNG_SRC}/dng_abort_sniffer.cpp
    ${DNG_SRC}/dng_area_task.cpp
    ${DNG_SRC}/dng_bad_pixels.cpp
    ${DNG_SRC}/dng_big_table.cpp
    ${DNG_SRC}/dng_bmff.cpp
    ${DNG_SRC}/dng_bottlenecks.cpp
    ${DNG_SRC}/dng_camera_profile.cpp
    ${DNG_SRC}/dng_color_space.cpp
    ${DNG_SRC}/dng_color_spec.cpp
    ${DNG_SRC}/dng_date_time.cpp
    ${DNG_SRC}/dng_exceptions.cpp
    ${DNG_SRC}/dng_exif.cpp
    ${DNG_SRC}/dng_file_stream.cpp
    ${DNG_SRC}/dng_filter_task.cpp
    ${DNG_SRC}/dng_fingerprint.cpp
    ${DNG_SRC}/dng_gain_map.cpp
    ${DNG_SRC}/dng_globals.cpp
    ${DNG_SRC}/dng_host.cpp
    ${DNG_SRC}/dng_hue_sat_map.cpp
    ${DNG_SRC}/dng_ifd.cpp
    ${DNG_SRC}/dng_image.cpp
    ${DNG_SRC}/dng_image_writer.cpp
    ${DNG_SRC}/dng_info.cpp
    ${DNG_SRC}/dng_iptc.cpp
    ${DNG_SRC}/dng_jpeg_image.cpp
    ${DNG_SRC}/dng_jpeg_memory_source.cpp
    ${DNG_SRC}/dng_jxl.cpp
    ${DNG_SRC}/dng_lens_correction.cpp
    ${DNG_SRC}/dng_linearization_info.cpp
    ${DNG_SRC}/dng_local_string.cpp
    ${DNG_SRC}/dng_lossless_jpeg.cpp
    ${DNG_SRC}/dng_lossless_jpeg_shared.cpp
    ${DNG_SRC}/dng_matrix.cpp
    ${DNG_SRC}/dng_memory.cpp
    ${DNG_SRC}/dng_memory_stream.cpp
    ${DNG_SRC}/dng_misc_opcodes.cpp
    ${DNG_SRC}/dng_mosaic_info.cpp
    ${DNG_SRC}/dng_mutex.cpp
    ${DNG_SRC}/dng_negative.cpp
    ${DNG_SRC}/dng_opcode_list.cpp
    ${DNG_SRC}/dng_opcodes.cpp
    ${DNG_SRC}/dng_orientation.cpp
    ${DNG_SRC}/dng_parse_utils.cpp
    ${DNG_SRC}/dng_pixel_buffer.cpp
    ${DNG_SRC}/dng_point.cpp
    ${DNG_SRC}/dng_preview.cpp
    ${DNG_SRC}/dng_pthread.cpp
    ${DNG_SRC}/dng_rational.cpp
    ${DNG_SRC}/dng_read_image.cpp
    ${DNG_SRC}/dng_rect.cpp
    ${DNG_SRC}/dng_ref_counted_block.cpp
    ${DNG_SRC}/dng_reference.cpp
    ${DNG_SRC}/dng_render.cpp
    ${DNG_SRC}/dng_resample.cpp
    ${DNG_SRC}/dng_safe_arithmetic.cpp
    ${DNG_SRC}/dng_shared.cpp
    ${DNG_SRC}/dng_simple_image.cpp
    ${DNG_SRC}/dng_spline.cpp
    ${DNG_SRC}/dng_stream.cpp
    ${DNG_SRC}/dng_string.cpp
    ${DNG_SRC}/dng_string_list.cpp
    ${DNG_SRC}/dng_tag_types.cpp
    ${DNG_SRC}/dng_temperature.cpp
    ${DNG_SRC}/dng_tile_iterator.cpp
    ${DNG_SRC}/dng_tone_curve.cpp
    ${DNG_SRC}/dng_update_meta.cpp
    ${DNG_SRC}/dng_utils.cpp
    ${DNG_SRC}/dng_xmp.cpp
    ${DNG_SRC}/dng_xmp_sdk.cpp
    ${DNG_SRC}/dng_xy_coord.cpp
)

add_library(dng_sdk STATIC ${DNG_SOURCES})

target_compile_definitions(dng_sdk PUBLIC
    qMacOS=1
    qWinOS=0
    qDNGUseStdInt=1
    Macintosh=1
    MAC_ENV=1
    BIB_MULTI_THREAD=1
    # XMP flags
    ENABLE_CPP_DOM_MODEL=0
    XML_STATIC=1
    HAVE_EXPAT_CONFIG_H=1
    XMP_StaticBuild=1
    XMP_64=1
    # JXL flags (match the client project xcconfig)
    DO_NOT_PREDEFINE__JXL_IS_DEBUG_BUILD=1
    JXL_INTERNAL_LIBRARY_BUILD=1
    JPEGXL_ENABLE_SKCMS=1
    JPEGXL_BUNDLE_SKCMS=0
    SKCMS_DISABLE_HSW=1
    SKCMS_DISABLE_SKX=1
    HWY_DISABLED_TARGETS=25952256
    # Build mode flags
    qDNGUseLibJPEG=1
    qDNGUseXMP=1
    qDNGXMPFiles=1
    qDNGXMPDocOps=0
    qDNGThreadSafe=1
)

target_include_directories(dng_sdk PUBLIC
    ${DNG_SRC}
    ${JPEG_SRC}
    ${JXL_CLIENT_INC}
    ${JXL_SRC}/lib/include
    ${JXL_SRC}/third_party/highway
    ${JXL_SRC}/third_party/skcms
    ${JXL_SRC}/third_party/brotli/c/include
    ${XMP_INC}
)

target_compile_options(dng_sdk PRIVATE
    -Wno-deprecated-declarations
    -Wno-unused-parameter
    -Wno-sign-compare
)

target_link_libraries(dng_sdk PUBLIC libjpeg_sdk)

# ---------------------------------------------------------------------------
# Our CLI tool
# ---------------------------------------------------------------------------

add_executable(dng_to_tiff src/dng_to_tiff.cpp)

target_link_libraries(dng_to_tiff PRIVATE
    dng_sdk
    "${XMP_LIB_DIR}/libXMPFilesStaticRelease.a"
    "${XMP_LIB_DIR}/libXMPCoreStaticRelease.a"
    "${JXL_LIB_DIR}/libjxl_release.a"
    "-framework CoreFoundation"
    "-framework CoreServices"
    "-framework Foundation"
)

install(TARGETS dng_to_tiff DESTINATION bin)
