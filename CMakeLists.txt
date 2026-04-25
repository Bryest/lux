cmake_minimum_required(VERSION 3.20)
project(lux LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
)
FetchContent_MakeAvailable(glm)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)

add_executable(${PROJECT_NAME} WIN32 ${SOURCES})

target_compile_definitions(${PROJECT_NAME} PRIVATE UNICODE _UNICODE)

target_link_libraries(${PROJECT_NAME} PRIVATE
    d3d12
    dxgi
    d3dcompiler
    glm::glm
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "src" FILES ${SOURCES})

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders.hlsl
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders.hlsl)
