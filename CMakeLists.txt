cmake_minimum_required(VERSION 3.16)
project(libgbasave VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

file(GLOB NOR_PROTOCOL_JSON CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/config/nor/protocols/*.json")
file(GLOB NOR_CHIP_JSON CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/config/nor/chips/*.json")

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/generated/nor_profile_db.h
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_nor_profiles.py --root ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_nor_profiles.py ${NOR_PROTOCOL_JSON} ${NOR_CHIP_JSON}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Validating/regenerating libgbasave NOR profile table"
    VERBATIM
)
add_custom_target(gbasave_nor_profiles_gen DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/generated/nor_profile_db.h)

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
add_dependencies(gbasave_objects gbasave_nor_profiles_gen)
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

add_custom_target(gbasave_runtime
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_runtime.py --root ${CMAKE_CURRENT_SOURCE_DIR} --variant all
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Rebuilding legacy and extended hardware-proven GBA runtime blobs"
    VERBATIM
)

add_custom_target(gbasave_nor_profiles DEPENDS gbasave_nor_profiles_gen)

add_custom_target(gbasave_gbabr_region_db
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_gbabr_region_db.py
            ${CMAKE_CURRENT_SOURCE_DIR}/data/GBABRPLANS.json
            --header ${CMAKE_CURRENT_SOURCE_DIR}/generated/gbabr_region_db.h
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Regenerating embedded shared GBABR safe-region index"
    VERBATIM
)

install(TARGETS gbasave_static gbasave_shared
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(DIRECTORY include/gbasave DESTINATION include)
