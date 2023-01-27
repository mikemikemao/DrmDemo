# Sets the minimum version of CMake required to build the native library.
cmake_minimum_required(VERSION 3.4.1)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11")

set(jnilibs ${CMAKE_SOURCE_DIR}/../jniLibs)
set(libname learn-drm)

include_directories(
        ${CMAKE_SOURCE_DIR}/
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/drm
        ${CMAKE_SOURCE_DIR}/utils
       )

link_directories(
        ${jnilibs}/${ANDROID_ABI}
        ${CMAKE_SOURCE_DIR}/utils/*.cpp
        )

file(GLOB src-files
        ${CMAKE_SOURCE_DIR}/*.cpp
        )

add_library( # Sets the name of the library.
             ${libname}
             # Sets the library as a shared library.
             SHARED

             # Provides a relative path to your source file(s).
             ${src-files}
        )

#导入第三方so包，并声明为 IMPORTED 属性，指明只是想把 so 导入到项目中
add_library(
        libdrm
        SHARED
        IMPORTED
)

#指明 so 库的路径，CMAKE_SOURCE_DIR 表示 CMakeLists.txt 的路径
set_target_properties(
        libdrm
        PROPERTIES IMPORTED_LOCATION
        ${CMAKE_SOURCE_DIR}/../jniLibs/arm64/libdrm.so )

#指明头文件路径，不然会提示找不到 so 的方法
include_directories(${CMAKE_SOURCE_DIR}/include)
#添加第三方头文件
#target_include_directories(test-lib PRIVATE ${CMAKE_SOURCE_DIR}/include)


set(third-party-libs

        )

set(native-libs
        android
        EGL
        GLESv3
        OpenSLES
        log
        mediandk
        m
        z
        )

target_link_libraries( # Specifies the target library.
                       ${libname}
                       libdrm
                       # Links the target library to the log library
                       # included in the NDK.
                       ${log-lib}
                       ${third-party-libs}
                       ${native-libs}
                       )