cmake_minimum_required (VERSION 3.8)

include_directories("../../cpymo")

find_package (SDL2 CONFIG REQUIRED)
find_package (FFMPEG REQUIRED)

file (GLOB CPYMO_BACKEND_SRC "*.h" "*.c")

add_executable (cpymo ${CPYMO_BACKEND_SRC})

if (APPLE AND NOT IOS AND ENABLE_ACCESSIBILITY)
	target_sources(cpymo PRIVATE "cpymo_backend_text_macos.m")
	target_link_libraries(cpymo PRIVATE "-framework AppKit")
endif ()

target_link_libraries(cpymo PRIVATE 
	SDL2::SDL2 
	SDL2::SDL2main 
	cpymolib
	${FFMPEG_LIBRARIES})


if (VCPKG_LIBRARY_LINKAGE EQUAL "static")
	target_link_libraries(cpymo PRIVATE SDL2::SDL2-static)
endif ()

if (CMAKE_SYSTEM_NAME MATCHES "Windows")
	target_sources (cpymo PRIVATE "pymo-icon-windows.rc")
	if (ENABLE_ACCESSIBILITY)
		set(TOLK_ROOT "${CMAKE_SOURCE_DIR}/third_party/tolk" CACHE PATH "Directory containing Tolk architecture folders")
		set(TOLK_INCLUDE_DIR "${TOLK_ROOT}/include" CACHE PATH "Directory containing Tolk.h")
		string(TOLOWER "${CMAKE_GENERATOR_PLATFORM}" TOLK_GENERATOR_PLATFORM)
		string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" TOLK_SYSTEM_PROCESSOR)
		if (TOLK_GENERATOR_PLATFORM STREQUAL "arm64" OR TOLK_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
			set(TOLK_ARCH arm64)
		elseif (CMAKE_SIZEOF_VOID_P EQUAL 8)
			set(TOLK_ARCH x64)
		else ()
			set(TOLK_ARCH x86)
		endif ()
		find_library(TOLK_LIBRARY NAMES Tolk PATHS "${TOLK_ROOT}/${TOLK_ARCH}/Release" NO_DEFAULT_PATH REQUIRED)
		file(GLOB TOLK_RUNTIME_DLLS "${TOLK_ROOT}/${TOLK_ARCH}/Release/*.dll")
		file(GLOB TOLK_RUNTIME_INIS "${TOLK_ROOT}/${TOLK_ARCH}/Release/*.ini")
		file(GLOB TOLK_RUNTIME_CONFS "${TOLK_ROOT}/${TOLK_ARCH}/Release/*.conf")
		target_include_directories(cpymo PRIVATE "${TOLK_INCLUDE_DIR}")
		target_link_libraries(cpymo PRIVATE "${TOLK_LIBRARY}")
		add_custom_command(TARGET cpymo POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TOLK_RUNTIME_DLLS} $<TARGET_FILE_DIR:cpymo>)
		add_custom_command(TARGET cpymo POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TOLK_RUNTIME_INIS} $<TARGET_FILE_DIR:cpymo>)
		add_custom_command(TARGET cpymo POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TOLK_RUNTIME_CONFS} $<TARGET_FILE_DIR:cpymo>)
		file(GLOB ACCESSIBILITY_WAVS "${CMAKE_CURRENT_SOURCE_DIR}/enter.wav" "${CMAKE_CURRENT_SOURCE_DIR}/menu.wav" "${CMAKE_CURRENT_SOURCE_DIR}/select.wav")
		add_custom_command(TARGET cpymo POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ACCESSIBILITY_WAVS} $<TARGET_FILE_DIR:cpymo>)
	endif ()
elseif (CMAKE_SYSTEM_NAME MATCHES "Linux")
	set(CMAKE_C_FLAGS "-D__LINUX__")
    target_link_libraries(cpymo PRIVATE X11)
	if (ENABLE_ACCESSIBILITY)
		file(GLOB ACCESSIBILITY_WAVS "${CMAKE_CURRENT_SOURCE_DIR}/enter.wav" "${CMAKE_CURRENT_SOURCE_DIR}/menu.wav" "${CMAKE_CURRENT_SOURCE_DIR}/select.wav")
		add_custom_command(TARGET cpymo POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ACCESSIBILITY_WAVS} $<TARGET_FILE_DIR:cpymo>)
	endif ()
elseif (APPLE AND NOT IOS)
	if (ENABLE_ACCESSIBILITY)
		file(GLOB ACCESSIBILITY_WAVS "${CMAKE_CURRENT_SOURCE_DIR}/enter.wav" "${CMAKE_CURRENT_SOURCE_DIR}/menu.wav" "${CMAKE_CURRENT_SOURCE_DIR}/select.wav")
		add_custom_command(TARGET cpymo POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ACCESSIBILITY_WAVS} $<TARGET_FILE_DIR:cpymo>)
	endif ()

