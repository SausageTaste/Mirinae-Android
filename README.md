# What is this?

This is an Android Studio project that uese [mirinae library](https://github.com/SausageTaste/mirinae) to build a rendering engine powered by Vulkan.

# How to build

You need to set some system environment variables

* `ANDROID_NDK_HOME`
  * You can install NDK via Android Studio SDK manager
  * For example `C:\Users\<username>\AppData\Local\Android\Sdk\ndk\27.0.12077973`
  * Different NDK version should work

* `VCPKG_ROOT`
  * Please refer to [vcpkg official documents](https://learn.microsoft.com/en-us/vcpkg/get_started/overview) to learn how to install it
  * We need to use [manifest mode](https://learn.microsoft.com/en-us/vcpkg/concepts/manifest-mode)
  * The value looks like `C:\Users\<username>\Documents\GitHub\vcpkg`
  * I cloned it with GitHub Desktop so that's why it's in this particular directory.
    You may choose whereever you want to put it in
 
* `VULKAN_SDK`
  * Install [LunarG Vulkan SDK](https://vulkan.lunarg.com/) and it will set the environment variable for you
 
After that, execute `<repo-root>/app/vcpkg_install.bat` script.
It will install all C++ external libraries via vcpkg.
It takes some time but you don't need to do it ever again, unless NDK version is changed or `vcpkg.json` file is updated.

Once vcpkg install is successful, you can build the app on Android Studio as usual.
