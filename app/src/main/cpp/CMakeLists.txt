# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

cmake_minimum_required(VERSION 3.22.1)

project("MirinaeAndroid")


if (ANDROID_ABI STREQUAL "arm64-v8a")
    set(mirinae_vcpkg_triplet "arm64-android")
elseif (ANDROID_ABI STREQUAL "x86_64")
    set(mirinae_vcpkg_triplet "x64-android")
else ()
    error("Not supported NDK architecture: ${ANDROID_ABI}")
endif()

set(mirinae_vcpkg_installed_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../../vcpkg_installed")
list(APPEND CMAKE_PREFIX_PATH "${mirinae_vcpkg_installed_dir}/${mirinae_vcpkg_triplet}/${mirinae_vcpkg_triplet}")
list(APPEND CMAKE_FIND_ROOT_PATH "${mirinae_vcpkg_installed_dir}/${mirinae_vcpkg_triplet}/${mirinae_vcpkg_triplet}")
list(APPEND CMAKE_MODULE_PATH "${mirinae_vcpkg_installed_dir}/${mirinae_vcpkg_triplet}/${mirinae_vcpkg_triplet}/share/stb")

get_cmake_property(_variableNames VARIABLES)
list (SORT _variableNames)
foreach (_variableName ${_variableNames})
    message(STATUS "${_variableName}=${${_variableName}}")
endforeach()


# Creates your game shared library. The name must be the same as the
# one used for loading in your Kotlin/Java or AndroidManifest.txt files.
add_library(mirinaeandroid SHARED
        filesys.cpp
        main.cpp
)

# Searches for a package provided by the game activity dependency
find_package(game-activity REQUIRED CONFIG)

set(DAL_USE_SUNGTOOLS_SUBMODULE ON CACHE BOOL "")
set(MIRINAE_USE_DALBARAGI_SUBMODULE ON CACHE BOOL "")
set(MIRINAE_BUILD_APP OFF CACHE BOOL "")
add_subdirectory(../../../../extern/mirinae ${PROJECT_BINARY_DIR}/libmirinae)

target_compile_definitions(mirinaeandroid PUBLIC VK_USE_PLATFORM_ANDROID_KHR)
target_compile_features(mirinaeandroid PUBLIC cxx_std_17)

# Configure libraries CMake uses to link your target library.
target_link_libraries(mirinaeandroid
        # The game activity
        game-activity::game-activity

        android
        log
        mirinae::core
)
