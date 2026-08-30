cmake_minimum_required(VERSION 3.21)

project(Mp4Parser)

include(submodules/my_tools/common.cmake)

add_subdirectory(submodules/my_tools my_tools)
add_subdirectory(submodules/Mp4ParseLib Mp4ParseLib)
add_subdirectory(submodules/imgui_base imgui)

aux_source_directory(src SRC_LIST)

if(CMAKE_SYSTEM_NAME MATCHES "Windows")
    configure_file(
        "${PROJECT_SOURCE_DIR}/resources/App.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/App.rc"
        @ONLY
    )
    add_executable(${PROJECT_NAME} WIN32 ${SRC_LIST} ${CMAKE_CURRENT_BINARY_DIR}/App.rc)

    if(CMAKE_COMPILER_IS_GNUCXX)
        target_link_options(${PROJECT_NAME} PRIVATE -mwindows)
    endif()
elseif(CMAKE_SYSTEM_NAME MATCHES "Darwin")
    set(MACOS_APP_ICON "${PROJECT_SOURCE_DIR}/resources/AppIcon.icns")
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

if(CMAKE_SYSTEM_NAME MATCHES "Windows")
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${PROJECT_SOURCE_DIR}/3rdlibrary/ffmpeg/include
        ${PROJECT_SOURCE_DIR}/3rdlibrary/lz4/include
        ${PROJECT_SOURCE_DIR}/resources
    )

    target_link_directories(${PROJECT_NAME} PRIVATE
        ${PROJECT_SOURCE_DIR}/3rdlibrary/ffmpeg/lib
        ${PROJECT_SOURCE_DIR}/3rdlibrary/lz4/lib
    )
elseif(CMAKE_SYSTEM_NAME MATCHES "Darwin")
    if(NOT EXISTS /opt/homebrew/opt/ffmpeg/include)
        message(FATAL_ERROR "FFmpeg is not installed")
    endif()
    if(NOT EXISTS /opt/homebrew/opt/lz4/include)
        message(FATAL_ERROR "LZ4 is not installed")
    endif()
    target_include_directories(${PROJECT_NAME} PRIVATE
        /opt/homebrew/opt/ffmpeg/include
        /opt/homebrew/opt/lz4/include
    )

    target_link_directories(${PROJECT_NAME} PRIVATE
        /opt/homebrew/opt/ffmpeg/lib
        /opt/homebrew/opt/lz4/lib
    )
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE imgui my_tools Mp4ParseLib avformat avcodec avutil swscale)
if(CMAKE_SYSTEM_NAME MATCHES "Windows")
    target_link_libraries(${PROJECT_NAME} PRIVATE liblz4_static)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE liblz4.a)
endif()

install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/bin)
if(CMAKE_SYSTEM_NAME MATCHES "Windows")
    file(GLOB FFMPEG_DLL_LIST ${PROJECT_SOURCE_DIR}/3rdlibrary/ffmpeg/bin/*.dll)
    set(DLL_LIST ${DLL_LIST}
        ${PROJECT_SOURCE_DIR}/submodules/imgui_base/3rdlibrary/freetype/bin/freetype.dll
        ${FFMPEG_DLL_LIST})
    install(FILES ${DLL_LIST} DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/bin)
endif()
