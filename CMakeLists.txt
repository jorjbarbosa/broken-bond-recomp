# broken_bond - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(broken_bond LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The isolated host build has no dependency on generated guest code or the SDK.
# It is also available through tools/native-renderer/isolated as a standalone build.
option(BROKENBOND_BUILD_NATIVE_BACKEND "Build the isolated native backend validation harness" OFF)
if(BROKENBOND_BUILD_NATIVE_BACKEND)
    add_subdirectory(tools/native-renderer/isolated)
endif()

include(generated/rexglue.cmake)

# Sources
set(BROKENBOND_SOURCES
    src/main.cpp
    src/input_replay.cpp
    src/graphics_device.cpp
    src/intro_skip.cpp
    src/dlc_install.cpp
    src/crash_report.cpp
    src/game_image.cpp
    src/mods/translation_data.cpp
    src/mods/translation_mod.cpp
)
# The host system services (gpu/common/platform.h), which the crash report also
# uses in the Xenos host; the Xenos adapter choice and the hybrid-GPU exports
# (graphics_device_win32.cpp) and the Wine check only on Windows.
if(WIN32)
    list(APPEND BROKENBOND_SOURCES src/graphics_device_win32.cpp src/gpu/common/platform_win32.cpp src/wine.cpp)
else()
    list(APPEND BROKENBOND_SOURCES src/gpu/common/platform_posix.cpp)
endif()

add_subdirectory(third_party/nlohmann_json)

# The shipping native build uses the Rise renderer adapted to Broken Bond,
# with Direct3D 12 and Vulkan APIs, both built by default. The OFF
# configuration is the separate Xenos investigation host. See docs/renderers.md.
option(BROKENBOND_NATIVE_RENDERER "Build the native game" OFF)
option(BROKENBOND_NATIVE_D3D12 "Build the native Direct3D 12 backend" ON)
option(BROKENBOND_NATIVE_VULKAN "Build the native Vulkan backend and SPIR-V shaders" ON)
option(BROKENBOND_NATIVE_TESTS "Build the native backend host tests" OFF)
# Outside Windows the game draws with the native renderer on Vulkan only: no
# Direct3D 12 and no Xenos host.
if(NOT WIN32)
    if(NOT BROKENBOND_NATIVE_RENDERER)
        message(FATAL_ERROR "Outside Windows the game draws with the native renderer: "
                            "set BROKENBOND_NATIVE_RENDERER=ON")
    endif()
    set(BROKENBOND_NATIVE_D3D12 OFF CACHE BOOL "Build the native Direct3D 12 backend" FORCE)
endif()
# The UI of the menus in the native build: RmlUi 6.3 over FreeType 2.15.0
# (third_party submodules), both static. FreeType without its optional
# dependencies (the fonts are plain TTF) and RmlUi without samples; RmlUi takes
# the FreeType target, not a FreeType of the system.
if(BROKENBOND_NATIVE_RENDERER)
    block()
        set(BUILD_SHARED_LIBS OFF)
        foreach(_dependency IN ITEMS ZLIB BZIP2 PNG HARFBUZZ BROTLI HVF)
            set(FT_DISABLE_${_dependency} ON)
        endforeach()
        add_subdirectory(third_party/freetype third_party/freetype EXCLUDE_FROM_ALL)
        add_library(Freetype::Freetype ALIAS freetype)
        set(CMAKE_DISABLE_FIND_PACKAGE_Freetype ON)
        set(RMLUI_SAMPLES OFF)
        set(RMLUI_FONT_ENGINE freetype)
        add_subdirectory(third_party/rmlui third_party/rmlui EXCLUDE_FROM_ALL)
    endblock()
    include(cmake/rmlui_patches.cmake)
endif()
if(BROKENBOND_NATIVE_RENDERER)
    if(NOT BROKENBOND_NATIVE_D3D12 AND NOT BROKENBOND_NATIVE_VULKAN)
        message(FATAL_ERROR "Enable at least one native backend")
    endif()
    list(APPEND BROKENBOND_SOURCES
         # The version of the port, for the window title.
         src/port_version.cpp
         # The host: configuration, guest kernel exports and the frame lifecycle.
         src/gpu/host/host.cpp src/gpu/host/kernel_video.cpp src/gpu/host/kernel_threading.cpp
         src/gpu/host/kernel_modules.cpp src/gpu/host/journal.cpp src/gpu/host/stop.cpp src/gpu/host/engines.cpp
         src/gpu/host/graphics.cpp src/gpu/host/frame_completion.cpp src/gpu/host/guest_hooks.cpp src/gpu/host/exports.cpp
         src/gpu/host/title_loading.cpp src/gpu/host/aspect.cpp
         # The backend: the D3D12 engine, its pipeline set, its dispatcher and the title-thread witness.
         src/gpu/backend/engine.cpp src/gpu/backend/failure_channel.cpp
         src/gpu/backend/pipeline_keys.cpp src/gpu/backend/pipeline_set.cpp src/gpu/backend/dispatcher.cpp
         # The D3D boundary of the native renderer, one unit per responsibility.
         src/gpu/d3d/hooks.cpp src/gpu/d3d/device.cpp src/gpu/d3d/resources.cpp
         src/gpu/d3d/pass.cpp src/gpu/d3d/draw.cpp src/gpu/d3d/state.cpp src/gpu/d3d/screen_layer.cpp
         src/gpu/d3d/present.cpp src/gpu/d3d/frame.cpp src/gpu/d3d/close.cpp src/gpu/d3d/query.cpp
         src/gpu/shaders/shaders.cpp src/gpu/shaders/package.cpp src/gpu/shaders/pipelines.cpp
         src/gpu/textures/textures.cpp src/gpu/textures/write_watch.cpp src/gpu/textures/formats.cpp
         src/gpu/textures/layout.cpp
         src/gpu/buffers/buffers.cpp src/gpu/buffers/vertex_layout.cpp
         src/gpu/diagnostics/frame_state.cpp src/gpu/diagnostics/census.cpp
         src/gpu/diagnostics/frame_trace.cpp src/gpu/diagnostics/start_evidence.cpp
         src/gpu/diagnostics/frame_capture.cpp src/gpu/diagnostics/measure.cpp
         # The UI layer of the Present, the SDK's immediate drawing over it and the UI
         # screen before the game.
         src/gpu/ui/layer.cpp src/gpu/ui/immediate_drawer.cpp src/gpu/ui/screen.cpp
         # The logic tick at a free frame rate and the transforms between the ticks (state interpolation).
         src/gpu/interpolation/tick.cpp src/gpu/interpolation/transforms.cpp
         # The graphics options, under the SDK names.
         src/gpu/settings.cpp
         # The shader cache embedded with #embed, generated by the build.
         src/gpu/shaders/shader_cache_data.cpp
         # Opt-in studies: the CRT's srand/rand and the world conditions
         # (REX_RAND_TRACE, both renderer modes); the audio frame cadence
         # (REX_AUDIO_TRACE, both renderer modes); the XMA stream sample accounting
         # (REX_XMA_STREAM_TRACE, both renderer modes); file reads and XMA voices
         # (REX_EXPORT_TRACE, native mode).
         src/studies/rand_trace.cpp src/studies/audio_trace.cpp src/studies/xma_stream_trace.cpp
         src/studies/export_trace.cpp
         src/studies/kernel_io_exports.cpp src/studies/kernel_xma_exports.cpp)
    # The diagnostics that need Windows (minidumps, page protection with a
    # vectored handler, suspended thread stacks, the swapchain proof's window
    # sizes, the CDB witness of the title thread); elsewhere they do nothing.
    if(WIN32)
        list(APPEND BROKENBOND_SOURCES
             src/gpu/diagnostics/crash_dump.cpp src/gpu/diagnostics/sampler.cpp
             src/gpu/diagnostics/heap_watch.cpp src/gpu/diagnostics/present_proof.cpp
             src/gpu/backend/title_witness.cpp)
    else()
        list(APPEND BROKENBOND_SOURCES src/gpu/diagnostics/inert_posix.cpp)
    endif()
endif()

if(WIN32)
    # The executable's icon, for the taskbar and Explorer.
    enable_language(RC)
    configure_file(src/broken_bond.rc.in "${CMAKE_CURRENT_BINARY_DIR}/broken_bond.rc" @ONLY)
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/broken_bond.rc" PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ui/broken_bond.ico")
    add_executable(broken_bond WIN32 ${BROKENBOND_SOURCES} "${CMAKE_CURRENT_BINARY_DIR}/broken_bond.rc")
else()
    add_executable(broken_bond ${BROKENBOND_SOURCES})
endif()

if(BROKENBOND_NATIVE_RENDERER)
    rexglue_setup_target(broken_bond)
else()
    # Retain the separate diagnostic host for renderer investigations.
    rexglue_setup_target(broken_bond GPU_PLUGINS xenos)
endif()
if(WIN32)
    target_link_libraries(broken_bond PRIVATE d3d12 dxgi)
endif()
target_include_directories(broken_bond PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(broken_bond PRIVATE broken_bond::json)
if(BROKENBOND_NATIVE_RENDERER)
    target_link_libraries(broken_bond PRIVATE RmlUi::Core)
    # The settings code of the menus: the configuration file, languages, key
    # bindings, graphics cards, folders, the update check and the desktop shortcut.
    add_library(broken_bond_menu_components STATIC
        src/menu/config_manager.cpp src/menu/folders.cpp src/menu/game_languages.cpp
        src/menu/graphics_devices.cpp src/menu/key_bindings.cpp src/menu/key_names.cpp
        src/menu/localization.cpp src/menu/update_checker.cpp src/menu/shortcut.cpp
        src/mods/translation_data.cpp
        # The menus: RmlUi over the SDK's immediate drawing, with the files of ui/ embedded.
        src/menu/text.cpp src/menu/panel.cpp src/menu/pad_input.cpp src/menu/input_gate.cpp
        src/menu/render_interface.cpp src/menu/assets.cpp src/menu/ui.cpp src/menu/settings.cpp
        # The DLC page: content folders, package staging and the folder picker.
        src/menu/content_manager.cpp src/menu/dlc.cpp src/menu/game_choice.cpp
        # The setup screen before the game: when it opens, the page, and the game data's identity.
        src/menu/setup_gate.cpp src/menu/setup_screen.cpp src/game_image.cpp
        # The setup screen's logo, read from the player's Naruto_clip.SRA.
        src/menu/game_logo.cpp
        # The four players' controls: profiles, hardware, the drivers they configure.
        src/controls/config.cpp src/controls/service.cpp src/controls/runtime.cpp src/controls/capture.cpp
        # Their page in the menus.
        src/menu/controls.cpp src/menu/controls_text.cpp)
    # The files that menu/assets.cpp embeds (#embed): a change rebuilds it.
    file(GLOB_RECURSE BROKENBOND_UI_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ui/*)
    set_source_files_properties(src/menu/assets.cpp PROPERTIES OBJECT_DEPENDS "${BROKENBOND_UI_FILES}")
    target_compile_features(broken_bond_menu_components PUBLIC cxx_std_23)
    target_include_directories(broken_bond_menu_components PUBLIC src)
    # The bounded zlib decoder of the SDK's stb (game_logo.cpp).
    target_include_directories(broken_bond_menu_components PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sdk/thirdparty/stb)
    target_link_libraries(broken_bond_menu_components PUBLIC SDL3::SDL3-static rex::tomlplusplus broken_bond::json
                          rex::runtime RmlUi::Core)
    if(WIN32)
        # The Wine check, the graphics cards of DXGI, the update check over
        # WinHTTP, the shortcut and the folder picker over COM.
        target_sources(broken_bond_menu_components PRIVATE src/wine.cpp)
        target_link_libraries(broken_bond_menu_components PUBLIC d3d12 dxgi winhttp ole32 shell32 uuid)
    endif()
    if(BROKENBOND_NATIVE_VULKAN)
        target_compile_definitions(broken_bond_menu_components PUBLIC BROKENBOND_MENU_NATIVE_VULKAN=1)
        target_include_directories(broken_bond_menu_components PRIVATE
            "${NATIVE_RENDERER_PLUME_SOURCE}/contrib/Vulkan-Headers/include")
    endif()
    if(NOT BROKENBOND_NATIVE_D3D12)
        target_compile_definitions(broken_bond_menu_components PUBLIC BROKENBOND_MENU_NATIVE_D3D12_DISABLED=1)
    endif()
    if(MSVC)
        target_compile_options(broken_bond_menu_components PRIVATE /utf-8)
    endif()
    target_link_libraries(broken_bond PRIVATE broken_bond_menu_components)
endif()

# The translation codec and package reader without the game (tests/translation_test.cpp).
add_executable(broken_bond_translation_test EXCLUDE_FROM_ALL
    tests/translation_test.cpp src/mods/translation_data.cpp)
target_include_directories(broken_bond_translation_test PRIVATE src)
target_link_libraries(broken_bond_translation_test PRIVATE rex::tomlplusplus broken_bond::json)
if(MSVC)
    target_compile_options(broken_bond_translation_test PRIVATE /utf-8)
endif()

# The strict package check before distribution (tools/localization/validate-package.cpp).
add_executable(broken_bond_validate_translation_package EXCLUDE_FROM_ALL
    tools/localization/validate-package.cpp src/mods/translation_data.cpp)
target_include_directories(broken_bond_validate_translation_package PRIVATE src)
target_link_libraries(broken_bond_validate_translation_package PRIVATE
    rex::tomlplusplus broken_bond::json)
if(MSVC)
    target_compile_options(broken_bond_validate_translation_package PRIVATE /utf-8)
endif()

if(BROKENBOND_NATIVE_RENDERER)
    include(${CMAKE_CURRENT_SOURCE_DIR}/tools/native-renderer/plume-engine.cmake)
    target_include_directories(broken_bond PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    if(BROKENBOND_NATIVE_D3D12)
        target_sources(broken_bond PRIVATE src/gpu/backend/d3d12/backend.cpp)
        target_link_libraries(broken_bond PRIVATE native_renderer_plume)
    else()
        target_compile_definitions(broken_bond PRIVATE BROKENBOND_NATIVE_D3D12_DISABLED=1)
    endif()
    set(BROKENBOND_NATIVE_VULKAN_SOURCES src/gpu/backend/vulkan/backend.cpp
        src/gpu/backend/vulkan/completion.cpp src/gpu/backend/vulkan/shader_contract.cpp)
    set(native_shader_options)
    if(BROKENBOND_NATIVE_VULKAN)
        target_sources(broken_bond PRIVATE ${BROKENBOND_NATIVE_VULKAN_SOURCES})
        target_link_libraries(broken_bond PRIVATE native_renderer_plume_vulkan)
        target_compile_definitions(broken_bond PRIVATE BROKENBOND_NATIVE_VULKAN=1)
        set(native_shader_options SPIRV PASSES src/gpu/backend)
    endif()
    target_compile_definitions(broken_bond PRIVATE BROKENBOND_NATIVE_RENDERER_BOOTSTRAP=1)
    # The version shown by the game when no broken_bond.version file sits next
    # to it (the packages write that file).
    set(BROKENBOND_VERSION "dev" CACHE STRING "Port version")
    target_compile_definitions(broken_bond PRIVATE BROKENBOND_VERSION="${BROKENBOND_VERSION}")
    # The shader cache generated from the local game files (game/xeshader.sdb).
    include(${CMAKE_CURRENT_SOURCE_DIR}/tools/native-renderer/shader-cache.cmake)
    brokenbond_shader_cache(broken_bond src/gpu/shaders/shader_cache_data.cpp ${native_shader_options})
    if(BROKENBOND_NATIVE_TESTS)
        if(NOT BROKENBOND_NATIVE_VULKAN)
            message(FATAL_ERROR "BROKENBOND_NATIVE_TESTS requires BROKENBOND_NATIVE_VULKAN")
        endif()
        enable_testing()
        include(tools/native-renderer/backend-tests.cmake)
        # The shared settings code of the menus (src/menu).
        foreach(_menu_test IN ITEMS key_bindings localization game_languages update_checker folders)
            add_executable(broken_bond_menu_${_menu_test}_test tests/${_menu_test}_test.cpp)
            target_link_libraries(broken_bond_menu_${_menu_test}_test PRIVATE broken_bond_menu_components)
            if(MSVC)
                target_compile_options(broken_bond_menu_${_menu_test}_test PRIVATE /utf-8)
            endif()
        endforeach()
        add_test(NAME menu_key_bindings COMMAND broken_bond_menu_key_bindings_test)
        add_test(NAME menu_localization COMMAND broken_bond_menu_localization_test)
        add_test(NAME menu_game_languages COMMAND broken_bond_menu_game_languages_test
            "${CMAKE_CURRENT_BINARY_DIR}/test-data/menu-game-languages")
        add_test(NAME menu_update_checker COMMAND broken_bond_menu_update_checker_test)
        add_test(NAME menu_folders COMMAND broken_bond_menu_folders_test)
        # The configuration file: load, save, the migrations and the API under Wine.
        add_executable(broken_bond_config_manager_test tests/config_manager_test.cpp)
        target_link_libraries(broken_bond_config_manager_test PRIVATE broken_bond_menu_components)
        if(MSVC)
            target_compile_options(broken_bond_config_manager_test PRIVATE /utf-8)
        endif()
        add_test(NAME config_manager COMMAND broken_bond_config_manager_test
            "${CMAKE_CURRENT_BINARY_DIR}/test-data/config-manager")
        # The DLC content operations (src/menu/content_manager.h) in a scratch root.
        add_executable(broken_bond_menu_dlc_test tests/dlc_test.cpp)
        target_link_libraries(broken_bond_menu_dlc_test PRIVATE broken_bond_menu_components)
        if(MSVC)
            target_compile_options(broken_bond_menu_dlc_test PRIVATE /utf-8)
        endif()
        add_test(NAME menu_dlc COMMAND broken_bond_menu_dlc_test "${CMAKE_CURRENT_BINARY_DIR}/test-data/menu-dlc")
        add_test(NAME menu_dlc_reopened COMMAND broken_bond_menu_dlc_test
            "${CMAKE_CURRENT_BINARY_DIR}/test-data/menu-dlc" --reopened)
        set_tests_properties(menu_dlc PROPERTIES FIXTURES_SETUP menu_dlc_tree)
        set_tests_properties(menu_dlc_reopened PROPERTIES FIXTURES_REQUIRED menu_dlc_tree)
        # The game data identification (src/game_image.h) on synthetic disc
        # images and folders, and on the repository's game folder when present.
        add_executable(broken_bond_game_image_test tests/game_image_test.cpp src/game_image.cpp)
        target_include_directories(broken_bond_game_image_test PRIVATE src)
        target_link_libraries(broken_bond_game_image_test PRIVATE rex::runtime)
        set(_game_image_real)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/game/default.xex")
            set(_game_image_real "ready=${CMAKE_CURRENT_SOURCE_DIR}/game")
        endif()
        add_test(NAME game_image COMMAND broken_bond_game_image_test
            "${CMAKE_CURRENT_BINARY_DIR}/test-data/game-image" ${_game_image_real})
        # When the setup screen opens (src/menu/setup_gate.h), and the game chosen
        # on it: saved to the .toml and identified on a worker (menu/game_choice.h).
        add_executable(broken_bond_setup_gate_test tests/setup_gate_test.cpp src/menu/setup_gate.cpp)
        target_include_directories(broken_bond_setup_gate_test PRIVATE src)
        add_test(NAME setup_gate COMMAND broken_bond_setup_gate_test)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/game/default.xex")
            add_executable(broken_bond_game_choice_test tests/game_choice_test.cpp)
            target_link_libraries(broken_bond_game_choice_test PRIVATE broken_bond_menu_components)
            add_test(NAME game_choice COMMAND broken_bond_game_choice_test
                "${CMAKE_CURRENT_BINARY_DIR}/test-data/game-choice" "${CMAKE_CURRENT_SOURCE_DIR}/game")
        endif()
        # The setup screen's logo reader on synthetic archives, a folder and a disc
        # image, then on the repository's game when present (no artwork in the test).
        add_executable(broken_bond_game_logo_test tests/game_logo_test.cpp)
        target_link_libraries(broken_bond_game_logo_test PRIVATE broken_bond_menu_components)
        set(_game_logo_real)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/game/Naruto_clip.SRA")
            set(_game_logo_real "${CMAKE_CURRENT_SOURCE_DIR}/game")
        endif()
        add_test(NAME game_logo_clean COMMAND "${CMAKE_COMMAND}" -E rm -rf "${CMAKE_CURRENT_BINARY_DIR}/test-data/game-logo")
        add_test(NAME game_logo COMMAND broken_bond_game_logo_test "${CMAKE_CURRENT_BINARY_DIR}/test-data/game-logo"
            ${_game_logo_real})
        set_tests_properties(game_logo_clean PROPERTIES FIXTURES_SETUP game_logo_dir)
        set_tests_properties(game_logo PROPERTIES FIXTURES_REQUIRED game_logo_dir)
        # The four players' controls (src/controls): profiles and their file, key
        # capture, the configured drivers, the hardware service on SDL's virtual
        # gamepads (with the SDK's SDL driver beside it: one static SDL), and the
        # recorded port assignment of the input replay.
        foreach(_controls_test IN ITEMS config capture runtime service replay)
            add_executable(broken_bond_controls_${_controls_test}_test tests/controls_${_controls_test}_test.cpp)
            target_link_libraries(broken_bond_controls_${_controls_test}_test PRIVATE broken_bond_menu_components)
        endforeach()
        if(WIN32)
            # SDL is static in the runtime DLL and in the test: the driver unit is
            # compiled beside the virtual fixture so both use one SDL instance.
            target_sources(broken_bond_controls_service_test PRIVATE sdk/src/input/sdl/sdl_input_driver.cpp)
        endif()
        target_sources(broken_bond_controls_replay_test PRIVATE src/input_replay.cpp)
        target_compile_definitions(broken_bond_controls_replay_test PRIVATE BROKENBOND_NATIVE_RENDERER_BOOTSTRAP=1)
        add_test(NAME controls_config COMMAND broken_bond_controls_config_test
            "${CMAKE_CURRENT_BINARY_DIR}/test-data/controls-config")
        add_test(NAME controls_capture COMMAND broken_bond_controls_capture_test)
        add_test(NAME controls_runtime COMMAND broken_bond_controls_runtime_test)
        add_test(NAME controls_service COMMAND broken_bond_controls_service_test)
        add_test(NAME controls_replay COMMAND broken_bond_controls_replay_test
            "${CMAKE_CURRENT_BINARY_DIR}/test-data/controls-replay")
        if(WIN32)
            # The menus' input gate over a driver that ignores the active callback
            # (as XInput and the replay do), the shader activity the menus show,
            # and the desktop shortcut (COM).
            add_executable(broken_bond_menu_input_test tests/menu_input_test.cpp)
            target_link_libraries(broken_bond_menu_input_test PRIVATE broken_bond_menu_components ole32 shell32)
            add_test(NAME menu_input COMMAND broken_bond_menu_input_test "${CMAKE_CURRENT_BINARY_DIR}/test-data/menu-input")
        else()
            # The SDK's POSIX waits: their timeouts and the sleep precision.
            add_executable(broken_bond_posix_wait_test tests/posix_wait_test.cpp)
            target_link_libraries(broken_bond_posix_wait_test PRIVATE rex::runtime)
            add_test(NAME posix_wait COMMAND broken_bond_posix_wait_test)
            # The settings restart's start once the game has exited (src/launch_arguments.h).
            add_executable(broken_bond_launch_restart_test tests/launch_restart_test.cpp)
            target_include_directories(broken_bond_launch_restart_test PRIVATE src)
            add_test(NAME launch_restart COMMAND broken_bond_launch_restart_test
                "${CMAKE_CURRENT_BINARY_DIR}/test-data/launch-restart")
        endif()
    endif()
    # The SDK runtime next to the executable on Linux, where the executable finds
    # it through its $ORIGIN RUNPATH; the SDK stages it itself only on Windows and macOS.
    if(UNIX AND NOT APPLE AND TARGET rex::runtime)
        add_custom_command(TARGET broken_bond POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:rex::runtime> $<TARGET_FILE_DIR:broken_bond>
            VERBATIM)
    endif()
endif()

if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_link_options(broken_bond PRIVATE -Xlinker /DEBUG)
endif()
