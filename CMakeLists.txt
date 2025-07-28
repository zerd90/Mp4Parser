# 最低CMake版本要求
cmake_minimum_required(VERSION 3.21)

# 项目名称
project(Mp4Parser)

include(my_tools/common.cmake)

execute_process(
	COMMAND git submodule update --init --depth 1
	WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)

# set INCLUDE_DIRECTORIES in every subdirectorys
add_subdirectory(my_tools my_tools)
add_subdirectory(Mp4ParseLib Mp4ParseLib)

# Set This To Your FFmpeg header/library path
set(INCLUDE_DIRECTORIES ${INCLUDE_DIRECTORIES}
	"D:/Files/3rdparty/include"
)
link_directories("D:/Files/3rdparty/lib")

# Uncomment the following lines if you want to use Freetype for GUI Font functions
set(USING_FREETYPE ON CACHE BOOL "Enable freetype" FORCE)
set(INCLUDE_DIRECTORIES ${INCLUDE_DIRECTORIES}
	"D:/Files/3rdparty/include/freetype"
)

add_subdirectory(imgui_base imgui)

set(LINK_LIBRARIES ${LINK_LIBRARIES} avformat avcodec avutil swscale)

include_directories(${INCLUDE_DIRECTORIES})

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
else()
	add_executable(${PROJECT_NAME} ${SRC_LIST})
endif()

target_link_libraries(${PROJECT_NAME} ${LINK_LIBRARIES})

install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/bin)
