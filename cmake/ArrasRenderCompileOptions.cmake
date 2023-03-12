# Copyright 2023 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

function(ArrasRender_cxx_compile_options target)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target}
            PRIVATE
                -fabi-version=6                 # corrects the promotion behavior of C++11 scoped enums and the mangling of template argument packs.
                -fexceptions                    # Enable exception handling.
                -fno-omit-frame-pointer         # TODO: add a note
                -fno-strict-aliasing            # TODO: add a note
                -fpermissive                    # Downgrade some diagnostics about nonconformant code from errors to warnings.
                -march=core-avx2                # Specify the name of the target architecture
                -mavx                           # x86 options
                -mfma                           # x86 options
                -msse                           # x86 options
                -pipe                           # Use pipes rather than intermediate files.
                -pthread                        # Define additional macros required for using the POSIX threads library.
                -w                              # Inhibit all warning messages.
                -Wall                           # Enable most warning messages.
                -Wcast-align                    # Warn about pointer casts which increase alignment.
                -Wcast-qual                     # Warn about casts which discard qualifiers.
                -Wdisabled-optimization         # Warn when an optimization pass is disabled.
                -Wextra                         # This enables some extra warning flags that are not enabled by -Wall
                -Woverloaded-virtual            # Warn about overloaded virtual function names.
                -Wno-conversion                 # Disable certain warnings that are enabled by -Wall
                -Wno-sign-compare               # Disable certain warnings that are enabled by -Wall
                -Wno-switch                     # Disable certain warnings that are enabled by -Wall
                -Wno-system-headers             # Disable certain warnings that are enabled by -Wall
                -Wno-unused-parameter           # Disable certain warnings that are enabled by -Wall

                $<$<CONFIG:RELWITHDEBINFO>:
                    -O3                         # the default is -O2 for RELWITHDEBINFO
                >
        )
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL Clang)
        target_compile_options(${target}
            # TODO: Some if not all of these should probably be PUBLIC
            PRIVATE
                -march=core-avx2                # Specify the name of the target architecture
                -fdelayed-template-parsing      # Shader.h has a template method that uses a moonray class which is no available to scene_rdl2 and is only used in moonray+
                -Wno-deprecated-declarations    # disable auto_ptr deprecated warnings from log4cplus-1.
                -Wno-unused-value               # caused by opt-debug build and MNRY_VERIFY.
        )
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL Intel)
        target_compile_options(${target}
            # TODO: Some if not all of these should probably be PUBLIC
            PRIVATE
                -march=core-avx2                # Specify the name of the target architecture
        )
    endif()
endfunction()

# ISPC compiler
function(SceneRdl2_ispc_compile_options target)
    target_compile_options(${target}
        PRIVATE
            --opt=force-aligned-memory          # always issue "aligned" vector load and store instructions
            --pic                               # Generate position-independent code.  Ignored for Windows target
            --werror                            # Treat warnings as errors
            # --wno-perf                          # Don't issue warnings related to performance-related issues

            $<$<CONFIG:DEBUG>:
                --dwarf-version=2               # use DWARF version 2 for debug symbols
            >

            $<$<CONFIG:RELWITHDEBINFO>:
                -O3                             # the default is -O2 for RELWITHDEBINFO
                --dwarf-version=2               # use DWARF version 2 for debug symbols
                --opt=disable-assertions        # disable all of the assertions
            >

            $<$<CONFIG:RELEASE>:
                --opt=disable-assertions        # disable all of the assertions
            >
    )
endfunction()
