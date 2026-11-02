cmake_minimum_required( VERSION 3.5 )

#######################################################################
# Vendored, trimmed-down build of eti-cmdline (https://github.com/JvanKatwijk/eti-stuff),
# by Jan van Katwijk, GPL-2.0-or-later. Bundled into dablin so that live SDR reception
# works without a separate manual install of eti-cmdline (see contrib/eti-cmdline/NOTICE.md).
#
# Only the RTL-SDR input device is kept; all other device backends (AirSpy, SDRplay,
# HackRF, LimeSDR, Pluto, file/XML readers, rtl_tcp) and the optional WAV-dump feature
# were removed, since dablin only ever launches this as its "eti-cmdline" live source for
# an RTL-SDR dongle, and keeping the unused backends would pull in unrelated SDK dependencies.
#######################################################################

set (objectName eti-cmdline-rtlsdr)
set (CMAKE_CXX_FLAGS "${CMAKE_XCC_FLAGS} -Wall -std=c++11 -ffast-math")

### make sure our local CMake Modules path comes first
list(INSERT CMAKE_MODULE_PATH 0 ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules)

find_package(PkgConfig)
find_package(LibRTLSDR)
if (NOT LIBRTLSDR_FOUND)
    message(FATAL_ERROR "please install librtlsdr (librtlsdr-dev)")
endif ()

find_library (PTHREADS pthread)
if (NOT(PTHREADS))
   message (FATAL_ERROR "please install libpthread")
endif ()
set (extraLibs ${PTHREADS} ${LIBRTLSDR_LIBRARIES})

add_definitions (-DHAVE_RTLSDR)

include_directories (
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/includes
    ${CMAKE_CURRENT_SOURCE_DIR}/includes/ofdm
    ${CMAKE_CURRENT_SOURCE_DIR}/includes/eti-handling
    ${CMAKE_CURRENT_SOURCE_DIR}/includes/eti-handling/viterbi-spiral
    ${CMAKE_CURRENT_SOURCE_DIR}/includes/support
    ${CMAKE_CURRENT_SOURCE_DIR}/includes/support/fft
    ${CMAKE_CURRENT_SOURCE_DIR}/devices
    ${CMAKE_CURRENT_SOURCE_DIR}/devices/rtlsdr-handler
    ${LIBRTLSDR_INCLUDE_DIR}
)

set (${objectName}_SRCS
    main.cpp
    eti-class.cpp
    src/ofdm/ofdm-processor.cpp
    src/ofdm/phasereference.cpp
    src/ofdm/phasetable.cpp
    src/ofdm/freq-interleaver.cpp
    src/eti-handling/semaphore.cpp
    src/eti-handling/eti-generator.cpp
    src/eti-handling/viterbi-spiral/viterbi-spiral.cpp
    src/eti-handling/viterbi-spiral/spiral-no-sse.c
    src/eti-handling/fic-handler.cpp
    src/eti-handling/fib-processor.cpp
    src/eti-handling/protection.cpp
    src/eti-handling/eep-protection.cpp
    src/eti-handling/uep-protection.cpp
    src/eti-handling/protTables.cpp
    src/eti-handling/charsets.cpp
    src/support/band-handler.cpp
    src/support/dab-params.cpp
    src/support/fft/fft.cpp
    src/support/fft/kiss_fft.c
    src/support/fft/kiss_fftr.c
    devices/device-handler.cpp
    devices/rtlsdr-handler/rtlsdr-handler.cpp
)

add_executable (${objectName} ${${objectName}_SRCS})
target_link_libraries (${objectName} ${extraLibs} ${CMAKE_DL_LIBS})

# placed next to dablin_gtk itself, so it can be found relative to the running executable
# without needing to be separately installed anywhere
set_target_properties (${objectName} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${DABLIN_RUNTIME_OUTPUT_DIRECTORY}
)
