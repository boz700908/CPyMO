cmake_minimum_required (VERSION 3.8)

include_directories("../../cpymo")

find_package (SDL2 CONFIG REQUIRED)
find_package (FFMPEG REQUIRED)

file (GLOB CPYMO_BACKEND_SRC "*.h" "*.c")

add_executable (cpymo ${CPYMO_BACKEND_SRC})

# 把随包分发的 WAV 编进可执行文件，供单文件分发（.exe / 单二进制 / .app 内的程序）使用。
# 运行时仍优先读文件，找不到才用内嵌数据。
find_package (Python3 COMPONENTS Interpreter QUIET)
set (CPYMO_EMBED_FILES "")
foreach (wav startup.wav enter.wav menu.wav select.wav)
	if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${wav}")
		list (APPEND CPYMO_EMBED_FILES "${wav}=${CMAKE_CURRENT_SOURCE_DIR}/${wav}")
	endif ()
endforeach ()
if (Python3_Interpreter_FOUND AND CPYMO_EMBED_FILES)
	execute_process (
		COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/embed_assets.py"
			"${CMAKE_CURRENT_BINARY_DIR}/cpymo_embedded_assets_data.c" ${CPYMO_EMBED_FILES}
		RESULT_VARIABLE CPYMO_EMBED_RESULT)
	if (NOT CPYMO_EMBED_RESULT EQUAL 0)
		message (FATAL_ERROR "生成内嵌资源失败")
	endif ()
	target_sources (cpymo PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/cpymo_embedded_assets_data.c")
	target_compile_definitions (cpymo PRIVATE CPYMO_EMBEDDED_ASSETS)
	message (STATUS "内嵌资源: ${CPYMO_EMBED_FILES}")
else ()
	message (STATUS "内嵌资源: 未启用（缺少 Python3 或没有 wav）")
endif ()

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
		target_compile_definitions(cpymo PRIVATE -DENABLE_TEXT_EXTRACT_COPY_TO_CLIPBOARD)
		set(TOLK_ROOT "${CMAKE_SOURCE_DIR}/third_party/tolk" CACHE PATH "Directory containing Tolk architecture folders")
		set(TOLK_INCLUDE_DIR "${TOLK_ROOT}/include" CACHE PATH "Directory containing Tolk.h")
		string(TOLOWER "${CMAKE_GENERATOR_PLATFORM}" TOLK_GENERATOR_PLATFORM)
		string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" TOLK_SYSTEM_PROCESSOR)
		if (TOLK_GENERATOR_PLATFORM STREQUAL "arm64ec" OR TOLK_SYSTEM_PROCESSOR STREQUAL "arm64ec")
			set(TOLK_ARCH arm64ec)
		elseif (TOLK_GENERATOR_PLATFORM STREQUAL "arm64" OR TOLK_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
			set(TOLK_ARCH arm64)
		elseif (TOLK_GENERATOR_PLATFORM STREQUAL "x64" OR TOLK_GENERATOR_PLATFORM STREQUAL "amd64")
			set(TOLK_ARCH x64)
		elseif (TOLK_GENERATOR_PLATFORM STREQUAL "win32" OR TOLK_GENERATOR_PLATFORM STREQUAL "x86")
			set(TOLK_ARCH x86)
		elseif (CMAKE_SIZEOF_VOID_P EQUAL 8)
			set(TOLK_ARCH x64)
		else ()
			set(TOLK_ARCH x86)
		endif ()
		if (NOT EXISTS "${TOLK_ROOT}/${TOLK_ARCH}/Release")
			message(WARNING "Tolk has no ${TOLK_ARCH} build, falling back to x64")
			set(TOLK_ARCH x64)
		endif ()
		message(STATUS "Tolk architecture: ${TOLK_ARCH} (generator platform: ${TOLK_GENERATOR_PLATFORM}, system processor: ${TOLK_SYSTEM_PROCESSOR})")
		file(GLOB TOLK_RUNTIME_DLLS "${TOLK_ROOT}/${TOLK_ARCH}/Release/*.dll")
		file(GLOB TOLK_RUNTIME_INIS "${TOLK_ROOT}/${TOLK_ARCH}/Release/*.ini")
		file(GLOB TOLK_RUNTIME_CONFS "${TOLK_ROOT}/${TOLK_ARCH}/Release/*.conf")
		target_include_directories(cpymo PRIVATE "${TOLK_INCLUDE_DIR}")
		# Tolk 靠 DLL 工作，无法静态链接。这里不再用导入库，而是把整套运行时
		# 打进 exe 的 RCDATA 资源，由 cpymo_tolk_runtime.c 在首次用到语音时释放到
		# %LOCALAPPDATA%\CPyMO\runtime\ 再加载 —— 于是 Windows 版只发一个 exe。
		set (CPYMO_TOLK_RC "${CMAKE_CURRENT_BINARY_DIR}/cpymo_tolk_embedded.rc")
		set (CPYMO_TOLK_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/cpymo_tolk_manifest.txt")
		file (WRITE "${CPYMO_TOLK_MANIFEST}" "")
		set (CPYMO_TOLK_RC_TEXT "// 由 CMakeLists 生成：内嵌 Tolk 运行时\n")
		set (CPYMO_TOLK_INDEX 0)
		foreach (tolk_file ${TOLK_RUNTIME_DLLS} ${TOLK_RUNTIME_INIS} ${TOLK_RUNTIME_CONFS})
			get_filename_component (tolk_name "${tolk_file}" NAME)
			file (APPEND "${CPYMO_TOLK_MANIFEST}" "${tolk_name}\n")
			math (EXPR tolk_id "${CPYMO_TOLK_INDEX} + 1000")
			string (REPLACE "/" "\\\\" tolk_path "${tolk_file}")
			string (APPEND CPYMO_TOLK_RC_TEXT "${tolk_id} RCDATA \"${tolk_path}\"\n")
			math (EXPR CPYMO_TOLK_INDEX "${CPYMO_TOLK_INDEX} + 1")
		endforeach ()
		string (REPLACE "/" "\\\\" tolk_manifest_path "${CPYMO_TOLK_MANIFEST}")
		string (APPEND CPYMO_TOLK_RC_TEXT "100 RCDATA \"${tolk_manifest_path}\"\n")
		file (WRITE "${CPYMO_TOLK_RC}" "${CPYMO_TOLK_RC_TEXT}")
		target_sources (cpymo PRIVATE "${CPYMO_TOLK_RC}")
		message (STATUS "Tolk: ${CPYMO_TOLK_INDEX} 个运行时文件内嵌进 exe（${TOLK_ARCH}）")
		file(GLOB ACCESSIBILITY_WAVS "${CMAKE_CURRENT_SOURCE_DIR}/enter.wav" "${CMAKE_CURRENT_SOURCE_DIR}/menu.wav" "${CMAKE_CURRENT_SOURCE_DIR}/select.wav")
		if (ACCESSIBILITY_WAVS)
			add_custom_command(TARGET cpymo POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ACCESSIBILITY_WAVS} $<TARGET_FILE_DIR:cpymo>)
		endif ()
	endif ()
elseif (CMAKE_SYSTEM_NAME MATCHES "Linux")
    target_link_libraries(cpymo PRIVATE X11)
	if (ENABLE_ACCESSIBILITY)
		target_compile_definitions(cpymo PRIVATE -DENABLE_TEXT_EXTRACT_LINUX_ACCESSIBILITY)
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
endif ()

# 启动音乐《Song of PyMO》：与无障碍无关，所有桌面平台都随包分发
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/startup.wav")
	add_custom_command(TARGET cpymo POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_SOURCE_DIR}/startup.wav" $<TARGET_FILE_DIR:cpymo>)
endif ()