# Copyright 2023-2024 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

# Configure sessiondef files so the library names are OS appropriate
set(COMPUTATION_PROGMCRT_LIB ${CMAKE_SHARED_LIBRARY_PREFIX}computation_progmcrt${CMAKE_SHARED_LIBRARY_SUFFIX})
configure_file(mcrt_progressive.sessiondef.in mcrt_progressive.sessiondef)

install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        DESTINATION .
        FILES_MATCHING PATTERN "*.sessiondef"
)
