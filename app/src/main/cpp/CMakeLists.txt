# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

cmake_minimum_required(VERSION 3.22.1)

project("MirinaeAndroid")


set(CMAKE_FIND_DEBUG_MODE 1)
set(CMAKE_FIND_USE_CMAKE_PATH ON)
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed_x64-android/x64-android")
list(APPEND CMAKE_FIND_ROOT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed_x64-android/x64-android")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed_x64-android/x64-android/share/stb")

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

add_subdirectory(../../../../extern/mirinae ${PROJECT_BINARY_DIR}/libmirinae)

target_compile_definitions(mirinaeandroid PUBLIC VK_USE_PLATFORM_ANDROID_KHR)

# Configure libraries CMake uses to link your target library.
target_link_libraries(mirinaeandroid
        # The game activity
        game-activity::game-activity

        # EGL and other dependent libraries required for drawing
        # and interacting with Android system
        EGL
        GLESv3
        jnigraphics
        android
        log
        mirinae::core
)
