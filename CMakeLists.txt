cmake_minimum_required(VERSION 3.16)
project(libgbasave VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

file(GLOB NOR_PROTOCOL_JSON CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/config/nor/protocols/*.json")
file(GLOB NOR_CHIP_JSON CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/config/nor/chips/*.json")
file(GLOB SAVE_MEMORY_PROFILE_JSON CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/config/save_memory/profiles/*.json")

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/generated/nor_profile_db.h
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_nor_profiles.py --root ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_nor_profiles.py ${NOR_PROTOCOL_JSON} ${NOR_CHIP_JSON}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Validating/regenerating libgbasave NOR profile table"
    VERBATIM
)
add_custom_target(gbasave_nor_profiles_gen DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/generated/nor_profile_db.h)

# The same exact-plan table is consumed by the in-memory SaveEngine and by the
# bounded-memory streaming frontend.  The extended fields are additive; the
# historical crc/size/op fields remain at the front of each generated entry.
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/generated/gbabr_plan_db.h
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_gbabr_plan_db.py
            ${CMAKE_CURRENT_SOURCE_DIR}/data/GBABRPLANS.json
            --header ${CMAKE_CURRENT_SOURCE_DIR}/generated/gbabr_plan_db.h
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_gbabr_plan_db.py ${CMAKE_CURRENT_SOURCE_DIR}/data/GBABRPLANS.json
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Regenerating shared exact ROM analysis table"
    VERBATIM
)
add_custom_target(gbasave_exact_plan_db_gen DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/generated/gbabr_plan_db.h)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/generated/m36_assets.h
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_m36_assets.py
            ${CMAKE_CURRENT_SOURCE_DIR}/data/m36_assets
            --header ${CMAKE_CURRENT_SOURCE_DIR}/generated/m36_assets.h
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_m36_assets.py
        ${CMAKE_CURRENT_SOURCE_DIR}/data/m36_assets/manifest.json
        ${CMAKE_CURRENT_SOURCE_DIR}/data/m36_assets/sfw_save_stubs.bin
        ${CMAKE_CURRENT_SOURCE_DIR}/data/m36_assets/m36_sram_generic_gbs2_payload.bin
        ${CMAKE_CURRENT_SOURCE_DIR}/data/m36_assets/m36_eeprom_dual_rww_payload.bin
        ${CMAKE_CURRENT_SOURCE_DIR}/data/m36_assets/m36_compact_rww_payload.bin
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Validating/regenerating shared M36 direct-RWW runtime assets"
    VERBATIM
)
add_custom_target(gbasave_m36_assets_gen DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/generated/m36_assets.h)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/generated/save_memory_profile_db.h
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_save_memory_profiles.py --root ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_save_memory_profiles.py ${SAVE_MEMORY_PROFILE_JSON}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Validating/regenerating save-memory capability profiles"
    VERBATIM
)
add_custom_target(gbasave_save_memory_profiles_gen DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/generated/save_memory_profile_db.h)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/generated/save_memory_assets.h
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_save_memory_assets.py --root ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_save_memory_assets.py
        ${CMAKE_CURRENT_SOURCE_DIR}/data/save_memory_assets/sfw_save_stubs.bin
        ${CMAKE_CURRENT_SOURCE_DIR}/data/save_memory_assets/fram_flash512_inline.bin
        ${CMAKE_CURRENT_SOURCE_DIR}/data/save_memory_assets/fram_banked_runtime.bin
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Regenerating bounded-memory SRAM/FRAM runtime assets"
    VERBATIM
)
add_custom_target(gbasave_save_memory_assets_gen DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/generated/save_memory_assets.h)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/generated/superfw_patch_db.h
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_superfw_database.py
            --input ${CMAKE_CURRENT_SOURCE_DIR}/data/superfw_patches.db
            --output ${CMAKE_CURRENT_SOURCE_DIR}/generated/superfw_patch_db.h
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_superfw_database.py ${CMAKE_CURRENT_SOURCE_DIR}/data/superfw_patches.db
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Regenerating compact save/WAITCNT compatibility database"
    VERBATIM
)
add_custom_target(gbasave_compat_db_gen DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/generated/superfw_patch_db.h)

set(GBASAVE_ENGINE_SOURCES
    src/engine.cpp
    src/rom_image.cpp
    src/sha256.cpp
    src/save_types.cpp
    src/gbabr_plan.cpp
    src/save_library.cpp
    src/save_signature.cpp
    src/save_marker.cpp
    src/exact_plan_save_library.cpp
    src/nor_backends.cpp
    src/flash_library_profile.cpp
    src/flash_scanner.cpp
    src/hole_finder.cpp
    src/storage_layout.cpp
    src/storage_image.cpp
    src/runtime_image.cpp
    src/save_patcher.cpp
    src/patch_report.cpp
    src/m36_compat_patcher.cpp
)

add_library(gbasave_objects OBJECT ${GBASAVE_ENGINE_SOURCES})
add_dependencies(gbasave_objects gbasave_nor_profiles_gen gbasave_exact_plan_db_gen gbasave_m36_assets_gen)
set_target_properties(gbasave_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(gbasave_objects
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/generated
)

if(MSVC)
    target_compile_options(gbasave_objects PRIVATE /W4)
else()
    target_compile_options(gbasave_objects PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(gbasave_static STATIC $<TARGET_OBJECTS:gbasave_objects>)
set_target_properties(gbasave_static PROPERTIES OUTPUT_NAME gbasave)
target_include_directories(gbasave_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/generated)
add_library(gbasave::static ALIAS gbasave_static)

add_library(gbasave_shared SHARED $<TARGET_OBJECTS:gbasave_objects>)
set_target_properties(gbasave_shared PROPERTIES
    OUTPUT_NAME gbasave
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)
target_include_directories(gbasave_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/generated)
add_library(gbasave::shared ALIAS gbasave_shared)

# Allocation-free bounded-memory frontend for NDS/ESP32.  It is kept as a
# separate static target so desktop users of the normal C++ engine do not pull
# the constrained frontend or its SuperFW-derived scanner unless requested.
set(GBASAVE_STREAMING_SOURCES
    src/streaming/save_plan.cpp
    src/streaming/compat_database.cpp
    src/streaming/analyzer.cpp
    src/streaming/exact_plan.cpp
    src/streaming/nor_layout.cpp
    src/streaming/nor_patch.cpp
    src/streaming/nor_prepare.cpp
    src/streaming/save_memory.cpp
    src/save_memory_profiles.cpp
)
add_library(gbasave_streaming STATIC ${GBASAVE_STREAMING_SOURCES})
add_dependencies(gbasave_streaming
    gbasave_exact_plan_db_gen
    gbasave_m36_assets_gen
    gbasave_save_memory_profiles_gen
    gbasave_save_memory_assets_gen
    gbasave_compat_db_gen
)
target_include_directories(gbasave_streaming PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/generated)
set_target_properties(gbasave_streaming PROPERTIES OUTPUT_NAME gbasave_streaming)
if(MSVC)
    target_compile_options(gbasave_streaming PRIVATE /W4)
else()
    target_compile_options(gbasave_streaming PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_library(gbasave::streaming ALIAS gbasave_streaming)

add_custom_target(gbasave_runtime
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_runtime.py --root ${CMAKE_CURRENT_SOURCE_DIR} --variant all
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Rebuilding legacy and extended hardware-proven GBA runtime blobs"
    VERBATIM
)

add_custom_target(gbasave_nor_profiles DEPENDS gbasave_nor_profiles_gen)
add_custom_target(gbasave_save_memory_profiles DEPENDS gbasave_save_memory_profiles_gen)

add_custom_target(gbasave_gbabr_region_db
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_gbabr_region_db.py
            ${CMAKE_CURRENT_SOURCE_DIR}/data/GBABRPLANS.json
            --header ${CMAKE_CURRENT_SOURCE_DIR}/generated/gbabr_region_db.h
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Regenerating embedded shared GBABR safe-region index"
    VERBATIM
)

install(TARGETS gbasave_static gbasave_shared gbasave_streaming
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(DIRECTORY include/gbasave DESTINATION include)
