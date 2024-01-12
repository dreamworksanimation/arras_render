# Copyright 2023-2024 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

set(CmdName arras_render)
set(ExportGroup ${PROJECT_NAME}Targets)

set(CMAKE_AUTOMOC TRUE)

add_executable(${CmdName})

target_sources(${CmdName}
    PRIVATE
        CamPlayback.cc
        DebugConsoleSetup.cc
        encodingUtil.cc
        FreeCam.cc
        ImageView.cc
        main.cc
        outputRate.cc
        Scripting.cc
)

target_link_libraries(${CmdName}
    PUBLIC
        ArrasCore::sdk
        ArrasCore::message_api
        ArrasCore::arras4_log
        ArrasCore::client_api
        ArrasCore::core_messages
        McrtDataio::client_receiver
        McrtMessages::mcrt_messages
        SceneRdl2::render_util
        SceneRdl2::scene_rdl2
        Boost::chrono
        Boost::program_options
        ${IMATHHALF}
        JPEG::JPEG
        OpenImageIO::OpenImageIO
        pthread
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::Script)

# Set standard compile/link options
ArrasRender_cxx_compile_definitions(${CmdName})
ArrasRender_cxx_compile_features(${CmdName})
ArrasRender_cxx_compile_options(${CmdName})
ArrasRender_link_options(${CmdName})

install(TARGETS ${CmdName}
        EXPORT ${ExportGroup}
        RUNTIME DESTINATION bin)
