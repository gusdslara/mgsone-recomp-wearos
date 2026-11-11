cmake_minimum_required(VERSION 3.20)
project(MGS-WearOS C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# SDL3 configuration for Android Wear OS
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_RENDER ON CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR ON CACHE BOOL "" FORCE)
set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
set(SDL_TRAY OFF CACHE BOOL "" FORCE)
set(SDL_POWER ON CACHE BOOL "" FORCE)
set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
if(ANDROID)
    set(SDL_OPENGLES ON CACHE BOOL "" FORCE)
else()
    set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
endif()

add_subdirectory(externals/SDL)

set(MGS_UPSTREAM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/engine")
set(PS1_RUNTIME_DIR "${MGS_UPSTREAM_DIR}/ps1Runtime")

set(FMT_SOURCES
    ${MGS_UPSTREAM_DIR}/third_party/fmt/src/format.cc
    ${MGS_UPSTREAM_DIR}/third_party/fmt/src/os.cc
)

set(PS1_RUNTIME_SOURCES
    ${PS1_RUNTIME_DIR}/src/gte.cpp
    ${PS1_RUNTIME_DIR}/src/games/crash3_object_presenter.cpp
    ${PS1_RUNTIME_DIR}/src/games/crash3_scenery_detail.cpp
    ${PS1_RUNTIME_DIR}/src/games/crash3_scenery_refinement.cpp
    ${PS1_RUNTIME_DIR}/src/games/crash3_scenery_construction.cpp
    ${PS1_RUNTIME_DIR}/src/games/crash3_profile.cpp
    ${PS1_RUNTIME_DIR}/src/games/mgs_profile.cpp
    ${PS1_RUNTIME_DIR}/src/games/mgs_disc.cpp
    ${PS1_RUNTIME_DIR}/src/games/mgs_decomp.cpp
    ${PS1_RUNTIME_DIR}/src/games/mgs_decomp/texture.c
    ${PS1_RUNTIME_DIR}/src/games/mgs_asset_cache.cpp
    ${PS1_RUNTIME_DIR}/src/games/mgs_scene.cpp
    ${PS1_RUNTIME_DIR}/src/games/mgs_frame_interpolation.cpp
    ${PS1_RUNTIME_DIR}/src/games/mgs_pose.cpp
    ${PS1_RUNTIME_DIR}/src/games/mgs_mesh_replay.cpp
    ${PS1_RUNTIME_DIR}/src/games/mgs_source_material.cpp
    ${PS1_RUNTIME_DIR}/src/memory_access.cpp
    ${PS1_RUNTIME_DIR}/src/mips_interpreter.cpp
    ${PS1_RUNTIME_DIR}/src/mips_construction_lineage.cpp
    ${PS1_RUNTIME_DIR}/src/mips_value_flow.cpp
    ${PS1_RUNTIME_DIR}/src/metrics.cpp
    ${PS1_RUNTIME_DIR}/src/pgxp.cpp
    ${PS1_RUNTIME_DIR}/src/gool/emu_compat.cpp
    ${PS1_RUNTIME_DIR}/src/gool/gool_interp.cpp
    ${PS1_RUNTIME_DIR}/src/cdrom/cue_parser.cpp
    ${PS1_RUNTIME_DIR}/src/cdrom/bin_reader.cpp
    ${PS1_RUNTIME_DIR}/src/cdrom/iso9660.cpp
    ${PS1_RUNTIME_DIR}/src/cdrom/exe_extractor.cpp
    ${PS1_RUNTIME_DIR}/src/cdrom/virtual_fs.cpp
    ${PS1_RUNTIME_DIR}/src/cdrom/cdrom_controller.cpp
    ${PS1_RUNTIME_DIR}/src/cdrom/xa_adpcm.cpp
    ${PS1_RUNTIME_DIR}/src/bios/bios.cpp
    ${PS1_RUNTIME_DIR}/src/bios/kernel_threads.cpp
    ${PS1_RUNTIME_DIR}/src/bios/bios_access.cpp
    ${PS1_RUNTIME_DIR}/src/bios/syscall_a.cpp
    ${PS1_RUNTIME_DIR}/src/bios/syscall_b.cpp
    ${PS1_RUNTIME_DIR}/src/bios/syscall_c.cpp
    ${PS1_RUNTIME_DIR}/src/bios/heap.cpp
    ${PS1_RUNTIME_DIR}/src/bios/event_system.cpp
    ${PS1_RUNTIME_DIR}/src/bios/file_io.cpp
    ${PS1_RUNTIME_DIR}/src/gpu/gpu.cpp
    ${PS1_RUNTIME_DIR}/src/gpu/display_geometry.cpp
    ${PS1_RUNTIME_DIR}/src/gpu/enhanced_surface.cpp
    ${PS1_RUNTIME_DIR}/src/gpu/modern_frame_interpolation.cpp
    ${PS1_RUNTIME_DIR}/src/gpu/modern_replay.cpp
    ${PS1_RUNTIME_DIR}/src/gpu/modern_replay_execution_plan.cpp
    ${PS1_RUNTIME_DIR}/src/gpu/modern_replay_vulkan_bake.cpp
    ${PS1_RUNTIME_DIR}/src/games/mgs_screen_effects.cpp
    ${PS1_RUNTIME_DIR}/src/games/mgs_surface_effects.cpp
    ${PS1_RUNTIME_DIR}/src/gpu/modern_replay_vulkan_copy.cpp
    ${PS1_RUNTIME_DIR}/src/gpu/modern_replay_vulkan_diagnostics.cpp
    ${PS1_RUNTIME_DIR}/src/gpu/modern_replay_depth_family.cpp
    ${PS1_RUNTIME_DIR}/src/gpu/modern_replay_visibility_manifest.cpp
    ${PS1_RUNTIME_DIR}/src/gpu/modern_replay_visibility_audit.cpp
    ${PS1_RUNTIME_DIR}/src/gpu/modern_replay_visibility_stack.cpp
    ${PS1_RUNTIME_DIR}/src/gpu/modern_replay_preflight.cpp
    ${PS1_RUNTIME_DIR}/src/gpu/modern_replay_recorder.cpp
    ${PS1_RUNTIME_DIR}/src/gpu/modern_replay_submission.cpp
    ${PS1_RUNTIME_DIR}/src/gpu/vulkan_replay_capability.cpp
    ${PS1_RUNTIME_DIR}/src/spu/spu.cpp
    ${PS1_RUNTIME_DIR}/src/dma/dma.cpp
    ${PS1_RUNTIME_DIR}/src/input/memory_card_files.cpp
    ${PS1_RUNTIME_DIR}/src/input/input.cpp
    ${PS1_RUNTIME_DIR}/src/input/input_script.cpp
    ${PS1_RUNTIME_DIR}/src/input/tas_movie.cpp
    ${PS1_RUNTIME_DIR}/src/input/tas_route.cpp
    ${PS1_RUNTIME_DIR}/src/mdec/mdec.cpp
    ${PS1_RUNTIME_DIR}/src/timers/timers.cpp
    ${PS1_RUNTIME_DIR}/src/save_state.cpp
    ${PS1_RUNTIME_DIR}/src/save_files.cpp
    ${PS1_RUNTIME_DIR}/src/soak_probe.cpp
    ${PS1_RUNTIME_DIR}/src/config.cpp
    ${PS1_RUNTIME_DIR}/src/overlay_manager.cpp
    ${PS1_RUNTIME_DIR}/src/psyq/psyq_hle.cpp
    ${PS1_RUNTIME_DIR}/src/psyq/psyq_registry.cpp
    ${PS1_RUNTIME_DIR}/src/psyq/psyq_libapi.cpp
    ${PS1_RUNTIME_DIR}/src/psyq/psyq_libcd.cpp
    ${PS1_RUNTIME_DIR}/src/psyq/psyq_libetc.cpp
    ${PS1_RUNTIME_DIR}/src/psyq/psyq_libgte.cpp
    ${PS1_RUNTIME_DIR}/src/psyq/psyq_libgpu.cpp
    ${PS1_RUNTIME_DIR}/src/psyq/psyq_font.cpp
    ${PS1_RUNTIME_DIR}/src/psyq/psyq_pad.cpp
    ${PS1_RUNTIME_DIR}/src/psyq/psyq_libc.cpp
    ${PS1_RUNTIME_DIR}/src/psyq/psyq_state.cpp
)

file(GLOB RECOMP_SHARD_SRC CONFIGURE_DEPENDS
    "${PS1_RUNTIME_DIR}/src/recompiled_out.part*.cpp"
)

set(RECOMP_MAIN_SRC "${PS1_RUNTIME_DIR}/src/recompiled_out.cpp")

set_source_files_properties(
    engine/mgs_main.cpp
    engine/mgs_wear_input.c
    ${PS1_RUNTIME_SOURCES}
    PROPERTIES
    COMPILE_OPTIONS "-O3;-g0;-march=armv7-a;-mfpu=neon;-mfloat-abi=softfp;-DNDEBUG"
)
set_source_files_properties(${RECOMP_SHARD_SRC} PROPERTIES
    COMPILE_OPTIONS "-O2;-g0;-march=armv7-a;-mfpu=neon;-mfloat-abi=softfp;-DNDEBUG"
)
set_source_files_properties(${RECOMP_MAIN_SRC} PROPERTIES
    COMPILE_OPTIONS "-O0;-g0;-fno-inline-functions;-DNDEBUG"
)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/externals/SDL/include
    ${PS1_RUNTIME_DIR}/include
    ${PS1_RUNTIME_DIR}/src
    ${MGS_UPSTREAM_DIR}/third_party/fmt/include
    ${MGS_UPSTREAM_DIR}/third_party/toml11/include
)

set(MGS_SOURCES
    engine/mgs_main.cpp
    engine/mgs_wear_input.c
    ${FMT_SOURCES}
    ${PS1_RUNTIME_SOURCES}
    ${RECOMP_MAIN_SRC}
    ${RECOMP_SHARD_SRC}
)

if(ANDROID)
    add_library(mgsnative SHARED
        ${MGS_SOURCES}
    )

    find_library(log-lib log)
    find_library(android-lib android)
    find_library(gles3-lib GLESv3)

    target_link_libraries(mgsnative
        SDL3-shared
        ${log-lib}
        ${android-lib}
        ${gles3-lib}
    )
else()
    add_executable(mgsnative
        ${MGS_SOURCES}
    )
    target_link_libraries(mgsnative
        SDL3-shared
    )
endif()
