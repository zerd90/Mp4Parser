# 最低CMake版本要求
cmake_minimum_required(VERSION 3.21)

# 项目名称
project(Mp4Parser)

include(submodules/my_tools/common.cmake)

# set INCLUDE_DIRECTORIES in every subdirectorys
add_subdirectory(submodules/my_tools my_tools)
add_subdirectory(submodules/Mp4ParseLib Mp4ParseLib)

if(CMAKE_SYSTEM_NAME MATCHES "Windows")
    set(INCLUDE_DIRECTORIES ${INCLUDE_DIRECTORIES}
        ${CMAKE_SOURCE_DIR}/3rdlibrary/ffmpeg/include
        ${CMAKE_SOURCE_DIR}/3rdlibrary/lz4/include
        ${CMAKE_SOURCE_DIR}/resources
    )

    set(LINK_DIRECTORIES ${LINK_DIRECTORIES}
        ${CMAKE_SOURCE_DIR}/3rdlibrary/ffmpeg/lib
        ${CMAKE_SOURCE_DIR}/3rdlibrary/lz4/lib
    )
elseif(CMAKE_SYSTEM_NAME MATCHES "Darwin")
    if(NOT EXISTS /opt/homebrew/opt/ffmpeg/include)
        message(FATAL_ERROR "FFmpeg is not installed")
    endif()
    if(NOT EXISTS /opt/homebrew/opt/lz4/include)
        message(FATAL_ERROR "LZ4 is not installed")
    endif()
    set(INCLUDE_DIRECTORIES ${INCLUDE_DIRECTORIES}
        /opt/homebrew/opt/ffmpeg/include
        /opt/homebrew/opt/lz4/include
    )

    set(LINK_DIRECTORIES ${LINK_DIRECTORIES}
        /opt/homebrew/opt/ffmpeg/lib
        /opt/homebrew/opt/lz4/lib
    )
endif()

add_subdirectory(submodules/imgui_base imgui)

set(LINK_LIBRARIES ${LINK_LIBRARIES} avformat avcodec avutil swscale)
if(CMAKE_SYSTEM_NAME MATCHES "Windows")
    set(LINK_LIBRARIES ${LINK_LIBRARIES} liblz4_static)
else()
    set(LINK_LIBRARIES ${LINK_LIBRARIES} liblz4.a)
endif()

include_directories(${INCLUDE_DIRECTORIES})
link_directories(${LINK_DIRECTORIES})

aux_source_directory(src SRC_LIST)

if(CMAKE_SYSTEM_NAME MATCHES "Windows")
    configure_file(
        "${CMAKE_SOURCE_DIR}/resources/App.rc.in"
        "${CMAKE_SOURCE_DIR}/resources/App.rc"
        @ONLY
    )
    add_executable(${PROJECT_NAME} WIN32 ${SRC_LIST} ${CMAKE_SOURCE_DIR}/resources/app.rc)

    if(CMAKE_COMPILER_IS_GNUCXX)
        target_link_options(${PROJECT_NAME} PRIVATE -mwindows)
    endif()
elseif(CMAKE_SYSTEM_NAME MATCHES "Darwin")
    set(MACOS_APP_ICON "${CMAKE_SOURCE_DIR}/resources/AppIcon.icns")
    set_source_files_properties(${MACOS_APP_ICON} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${SRC_LIST} ${MACOS_APP_ICON})
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE_ICON_FILE "AppIcon.icns"
    )
else()
    add_executable(${PROJECT_NAME} ${SRC_LIST})
endif()

target_link_libraries(${PROJECT_NAME} ${LINK_LIBRARIES})

install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/bin)
if(CMAKE_SYSTEM_NAME MATCHES "Windows")
    file(GLOB FFMPEG_DLL_LIST ${CMAKE_SOURCE_DIR}/3rdlibrary/ffmpeg/bin/*.dll)
    set(DLL_LIST ${DLL_LIST} ${FFMPEG_DLL_LIST})
    install(FILES ${DLL_LIST} DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/bin)
endif()
