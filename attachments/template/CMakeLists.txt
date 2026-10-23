cmake_minimum_required(VERSION 3.29)

set(TEMPLATE_NAME "VulkanTutorial")

project(${TEMPLATE_NAME} LANGUAGES CXX)

option(
    ENABLE_CPP20_MODULE
    "Enable C++20 module support for Vulkan"
    OFF
)

if(ENABLE_CPP20_MODULE)
    set(CMAKE_CXX_SCAN_FOR_MODULES ON)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../CMake")

find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)
find_package(Vulkan 1.4.335 REQUIRED)
find_package(tinyobjloader REQUIRED)
find_package(tinygltf REQUIRED)
find_package(KTX REQUIRED)
find_package(stb REQUIRED)

find_program(
    SLANGC_EXECUTABLE
    NAMES slangc
    HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "$ENV{VULKAN_SDK}/bin"
    REQUIRED
)

set(STB_INCLUDEDIR "${stb_INCLUDE_DIRS}")
set(TEMPLATE_DIR "${CMAKE_BINARY_DIR}/${TEMPLATE_NAME}")

# ---------------------------------------------------------------------------
# Vulkan C++ module
# ---------------------------------------------------------------------------

if(ENABLE_CPP20_MODULE)

    add_library(VulkanCppModule)

    add_library(Vulkan::cppm ALIAS VulkanCppModule)

    target_compile_definitions(
        VulkanCppModule
        PUBLIC
            VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
            VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
    )

    target_include_directories(
        VulkanCppModule
        PUBLIC
            "${Vulkan_INCLUDE_DIR}"
    )

    target_link_libraries(
        VulkanCppModule
        PUBLIC
            Vulkan::Vulkan
    )

    set_target_properties(
        VulkanCppModule
        PROPERTIES
            CXX_STANDARD 20
            CXX_STANDARD_REQUIRED ON
    )

    if(MSVC)
        target_compile_options(
            VulkanCppModule
            PRIVATE
                /std:c++latest
                /permissive-
                /Zc:__cplusplus
                /EHsc
                /Zc:preprocessor
                /translateInclude
        )
    endif()

    target_sources(
        VulkanCppModule
        PUBLIC
            FILE_SET cxx_modules
            TYPE CXX_MODULES
            BASE_DIRS
                "${Vulkan_INCLUDE_DIR}"
            FILES
                "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm"
    )

else()

    add_library(VulkanCppModule INTERFACE)

    add_library(Vulkan::cppm ALIAS VulkanCppModule)

    target_link_libraries(
        VulkanCppModule
        INTERFACE
            Vulkan::Vulkan
    )

    target_compile_definitions(
        VulkanCppModule
        INTERFACE
            VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
            VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
    )

endif()

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------

add_executable(
    ${TEMPLATE_NAME}
    main.cpp
)

set_target_properties(
    ${TEMPLATE_NAME}
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${TEMPLATE_DIR}"
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
)

target_link_libraries(
    ${TEMPLATE_NAME}
    PRIVATE
        Vulkan::cppm
        glfw
)

target_include_directories(
    ${TEMPLATE_NAME}
    PRIVATE
        "${STB_INCLUDEDIR}"
)

if(ENABLE_CPP20_MODULE)
    target_compile_definitions(
        ${TEMPLATE_NAME}
        PRIVATE
            USE_CPP20_MODULES=1
    )
endif()

target_compile_definitions(
    ${TEMPLATE_NAME}
    PRIVATE
        VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS
)

# Run the program from the executable's directory.
if(WIN32 AND CMAKE_GENERATOR MATCHES "Visual Studio")
    set_target_properties(
        ${TEMPLATE_NAME}
        PROPERTIES
            VS_DEBUGGER_WORKING_DIRECTORY
                "$<TARGET_FILE_DIR:${TEMPLATE_NAME}>"
    )
endif()

# ---------------------------------------------------------------------------
# Slang shader compilation
# ---------------------------------------------------------------------------

set(SHADER_TARGET "${TEMPLATE_NAME}Shader")

set(SHADER_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/shader.slang"
)

# Temporary shader output inside the build directory.
set(SHADER_OUTPUT
    "${CMAKE_CURRENT_BINARY_DIR}/generated/slang.spv"
)

add_custom_command(
    OUTPUT
        "${SHADER_OUTPUT}"

    COMMAND
        "${CMAKE_COMMAND}"
        -E make_directory
        "${CMAKE_CURRENT_BINARY_DIR}/generated"

    COMMAND
        "${SLANGC_EXECUTABLE}"
        "${SHADER_SOURCE}"
        -target spirv
        -profile spirv_1_4
        -emit-spirv-directly
        -fvk-use-entrypoint-name
        -entry vertMain
        -entry fragMain
        -o "${SHADER_OUTPUT}"

    DEPENDS
        "${SHADER_SOURCE}"

    COMMENT "Compiling Slang shader"
    VERBATIM
)

add_custom_target(
    ${SHADER_TARGET}
    DEPENDS
        "${SHADER_OUTPUT}"
)

add_dependencies(
    ${TEMPLATE_NAME}
    ${SHADER_TARGET}
)

# Copy only the shader beside the executable.
add_custom_command(
    TARGET ${TEMPLATE_NAME}
    POST_BUILD

    COMMAND
        "${CMAKE_COMMAND}"
        -E make_directory
        "$<TARGET_FILE_DIR:${TEMPLATE_NAME}>/shaders"

    COMMAND
        "${CMAKE_COMMAND}"
        -E copy_if_different
        "${SHADER_OUTPUT}"
        "$<TARGET_FILE_DIR:${TEMPLATE_NAME}>/shaders/slang.spv"

    COMMENT "Copying shader beside executable"
)

# ---------------------------------------------------------------------------
# Assets remain in the original location
# ---------------------------------------------------------------------------

set(ASSET_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../assets"
)

file(
    COPY
        "${ASSET_DIR}/viking_room.obj"
    DESTINATION
        "${TEMPLATE_DIR}/models"
)

file(
    COPY
        "${ASSET_DIR}/viking_room.png"
    DESTINATION
        "${TEMPLATE_DIR}/textures"
)