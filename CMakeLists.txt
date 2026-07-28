cmake_minimum_required (VERSION 3.8)

project ("cpymo")

if (APPLE)
	enable_language(OBJC)
endif ()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build)

option(ENABLE_ACCESSIBILITY "Enable platform accessibility output" OFF)

add_definitions (-D_CRT_SECURE_NO_WARNINGS)
if (ENABLE_ACCESSIBILITY)
	add_definitions (-DENABLE_TEXT_EXTRACT)
	if (WIN32)
		add_definitions (-DENABLE_TEXT_EXTRACT_COPY_TO_CLIPBOARD)
	elseif (CMAKE_SYSTEM_NAME MATCHES "Linux")
		add_definitions (-DENABLE_TEXT_EXTRACT_LINUX_ACCESSIBILITY)
	endif ()
endif ()
add_definitions (-DENABLE_EXIT_CONFIRM)
add_definitions (-DLEAKCHECK)
add_definitions (-DLIMIT_WINDOW_SIZE_TO_SCREEN)

find_package (FFMPEG REQUIRED)

if (FFMPEG_FOUND)
else ()
	MESSAGE ("Can not find FFMPEG!")
endif (FFMPEG_FOUND)

include_directories (${FFMPEG_INCLUDE_DIRS})

add_subdirectory ("cpymo")
add_subdirectory ("cpymo-tool")
add_subdirectory ("cpymo-backends/sdl2")
