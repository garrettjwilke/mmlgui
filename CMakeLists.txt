cmake_minimum_required(VERSION 3.11)
project(mmlgui LANGUAGES C CXX)

# --- Check if mdsdrv.bin already exists FIRST (before any other setup) ---
# If it exists, we skip building clownassembler, sjasmplus, and salvador
set(MDSDRV_BIN_PATH ${CMAKE_SOURCE_DIR}/MDSDRV/out/mdsdrv.bin)
get_filename_component(MDSDRV_BIN_PATH_ABS ${MDSDRV_BIN_PATH} ABSOLUTE)

# Normalize paths for cross-platform compatibility
file(TO_CMAKE_PATH "${MDSDRV_BIN_PATH}" MDSDRV_BIN_PATH_NORM)
file(TO_CMAKE_PATH "${MDSDRV_BIN_PATH_ABS}" MDSDRV_BIN_PATH_ABS_NORM)

# Check if mdsdrv.bin already exists (e.g., checked into git)
# Try multiple path formats to handle Windows/MSYS2 path issues
set(MDSDRV_BIN_EXISTS FALSE)

# First try CMake's EXISTS check with various path formats
if(EXISTS "${MDSDRV_BIN_PATH}")
    set(MDSDRV_BIN_EXISTS TRUE)
    message(STATUS "mdsdrv.bin already exists, skipping build of clownassembler, sjasmplus, and salvador")
    message(STATUS "  Found at: ${MDSDRV_BIN_PATH}")
elseif(EXISTS "${MDSDRV_BIN_PATH_ABS}")
    set(MDSDRV_BIN_EXISTS TRUE)
    message(STATUS "mdsdrv.bin already exists, skipping build of clownassembler, sjasmplus, and salvador")
    message(STATUS "  Found at: ${MDSDRV_BIN_PATH_ABS}")
elseif(EXISTS "${MDSDRV_BIN_PATH_NORM}")
    set(MDSDRV_BIN_EXISTS TRUE)
    message(STATUS "mdsdrv.bin already exists, skipping build of clownassembler, sjasmplus, and salvador")
    message(STATUS "  Found at: ${MDSDRV_BIN_PATH_NORM}")
elseif(EXISTS "${MDSDRV_BIN_PATH_ABS_NORM}")
    set(MDSDRV_BIN_EXISTS TRUE)
    message(STATUS "mdsdrv.bin already exists, skipping build of clownassembler, sjasmplus, and salvador")
    message(STATUS "  Found at: ${MDSDRV_BIN_PATH_ABS_NORM}")
else()
    # Fallback: Use shell command to check (works better on MSYS2/Windows)
    # Try with bash/sh test command
    if(UNIX OR MINGW)
        execute_process(
            COMMAND sh -c "test -f '${MDSDRV_BIN_PATH}' && echo 'EXISTS' || echo 'NOT_EXISTS'"
            OUTPUT_VARIABLE SHELL_CHECK_RESULT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(SHELL_CHECK_RESULT STREQUAL "EXISTS")
            set(MDSDRV_BIN_EXISTS TRUE)
            message(STATUS "mdsdrv.bin already exists (shell check), skipping build of clownassembler, sjasmplus, and salvador")
            message(STATUS "  Found at: ${MDSDRV_BIN_PATH}")
        else()
            message(STATUS "mdsdrv.bin does not exist, will build clownassembler, sjasmplus, and salvador")
            message(STATUS "  CMAKE_SOURCE_DIR: ${CMAKE_SOURCE_DIR}")
            message(STATUS "  Checked: ${MDSDRV_BIN_PATH}")
            message(STATUS "  Checked: ${MDSDRV_BIN_PATH_ABS}")
            message(STATUS "  Shell check result: ${SHELL_CHECK_RESULT}")
        endif()
    else()
        message(STATUS "mdsdrv.bin does not exist, will build clownassembler, sjasmplus, and salvador")
        message(STATUS "  CMAKE_SOURCE_DIR: ${CMAKE_SOURCE_DIR}")
        message(STATUS "  Checked: ${MDSDRV_BIN_PATH}")
        message(STATUS "  Checked: ${MDSDRV_BIN_PATH_ABS}")
    endif()
endif()

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
find_package(OpenGL REQUIRED)

# macOS-specific: Help pkg-config find Homebrew packages
if(APPLE)
	# Set PKG_CONFIG_PATH to include Homebrew paths
	set(ENV{PKG_CONFIG_PATH} "/opt/homebrew/opt/glfw/lib/pkgconfig:/opt/homebrew/opt/cppunit/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
	# Also try /usr/local for older Homebrew installations
	if(EXISTS "/usr/local/opt/glfw/lib/pkgconfig")
		set(ENV{PKG_CONFIG_PATH} "/usr/local/opt/glfw/lib/pkgconfig:/usr/local/opt/cppunit/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
	endif()
endif()

pkg_check_modules(GLFW3 REQUIRED glfw3)
pkg_check_modules(CPPUNIT cppunit)

if(MINGW)
	option(LINK_STATIC_LIBS "link with static runtime libraries (MinGW only)" ON)
	if(LINK_STATIC_LIBS)
		set(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
		set(CMAKE_CXX_STANDARD_LIBRARIES "-static-libgcc -static-libstdc++ -lwsock32 -lws2_32 ${CMAKE_CXX_STANDARD_LIBRARIES}")
		#This gets already set by libvgm...
		#set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive")
		set(GLFW3_LIBRARIES -static ${GLFW3_STATIC_LIBRARIES} -dynamic)
	endif()
else()
	option(LINK_STATIC_LIBS "link with static runtime libraries (MinGW only)" OFF)
endif()

# Ensure ctrmml is on the rng-patterns branch before building
if(EXISTS "${CMAKE_SOURCE_DIR}/ctrmml/.git")
	execute_process(
		COMMAND git checkout rng-patterns
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/ctrmml
		RESULT_VARIABLE git_result
		OUTPUT_QUIET
		ERROR_QUIET
	)
	if(git_result EQUAL 0)
		message(STATUS "Checked out rng-patterns branch in ctrmml")
	else()
		message(WARNING "Failed to checkout rng-patterns branch in ctrmml (result: ${git_result})")
	endif()
endif()

add_subdirectory(ctrmml)

# Set C standard for libvgm compatibility (avoids C23 bool keyword conflict)
# libvgm uses a custom stdbool.h that typedefs bool, which conflicts with C23
if(NOT DEFINED CMAKE_C_STANDARD)
	set(CMAKE_C_STANDARD 90)
	set(CMAKE_C_STANDARD_REQUIRED OFF)
endif()

add_subdirectory(libvgm)

# --- Build clownassembler_asm68k using CMake ---
set(CLOWNASM_BUILD_DIR ${CMAKE_SOURCE_DIR}/MDSDRV/clownassembler/build)
set(CLOWNASM_EXE ${CMAKE_SOURCE_DIR}/MDSDRV/clownassembler/clownassembler_asm68k)

# Use the check result from the top of the file
if(MDSDRV_BIN_EXISTS)
    # Create dummy targets that do nothing - the files already exist
    add_custom_target(clownassembler_asm68k_bin
        COMMENT "Skipping clownassembler_asm68k build (mdsdrv.bin already exists)"
        VERBATIM
    )
    
    add_custom_target(mdsdrv_bin
        COMMENT "Skipping mdsdrv.bin build (already exists)"
        VERBATIM
    )
else()
    # Build clownassembler_asm68k using CMake
    add_custom_command(
        OUTPUT ${CLOWNASM_EXE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CLOWNASM_BUILD_DIR}
        COMMAND ${CMAKE_COMMAND} -S ${CMAKE_SOURCE_DIR}/MDSDRV/clownassembler -B ${CLOWNASM_BUILD_DIR}
        COMMAND ${CMAKE_COMMAND} --build ${CLOWNASM_BUILD_DIR} --target clownassembler_asm68k
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CLOWNASM_BUILD_DIR}/clownassembler_asm68k ${CLOWNASM_EXE}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/MDSDRV/clownassembler
        COMMENT "Building clownassembler_asm68k with CMake"
        VERBATIM
    )

    add_custom_target(clownassembler_asm68k_bin DEPENDS ${CLOWNASM_EXE})

    # Generate mdsdrv.bin if it does not exist or is out of date
    add_custom_command(
        OUTPUT ${MDSDRV_BIN_PATH}
        COMMAND ${CMAKE_MAKE_PROGRAM} -C ${CMAKE_SOURCE_DIR}/MDSDRV mdsdrv
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/MDSDRV
        DEPENDS ${CLOWNASM_EXE}
        COMMENT "Building mdsdrv.bin"
        VERBATIM
    )

    # Custom target that depends on the generated binary
    add_custom_target(mdsdrv_bin DEPENDS ${MDSDRV_BIN_PATH})
endif()

# --- Embed mdsdrv.bin as C++ source ---
set(EMBEDDED_MDSDRV_SRC ${CMAKE_BINARY_DIR}/embedded_mdsdrv_bin.cpp)

add_custom_command(
    OUTPUT ${EMBEDDED_MDSDRV_SRC}
    COMMAND ${CMAKE_COMMAND}
        -DINPUT_FILE=${MDSDRV_BIN_PATH}
        -DOUTPUT_FILE=${EMBEDDED_MDSDRV_SRC}
        -DVARIABLE_NAME=embedded_mdsdrv_bin_data
        -P ${CMAKE_CURRENT_SOURCE_DIR}/embed_binary.cmake
    DEPENDS ${MDSDRV_BIN_PATH}   # depends on actual file, not the target
    COMMENT "Embedding mdsdrv.bin into executable"
    VERBATIM
)


add_library(gui
	imgui/imgui.cpp
	imgui/imgui_demo.cpp
	imgui/imgui_draw.cpp
	imgui/imgui_widgets.cpp
	imgui/examples/imgui_impl_glfw.cpp
	imgui/examples/imgui_impl_opengl3.cpp
	imgui/examples/libs/gl3w/GL/gl3w.c
	imgui/addons/imguifilesystem/imguifilesystem.cpp
	ImGuiColorTextEdit/TextEditor.cpp)

target_include_directories(gui PUBLIC
	imgui
	imgui/examples
	imgui/examples/libs/gl3w
	ImGuiColorTextEdit
	${GLFW3_INCLUDE_DIRS})

target_link_directories(gui PUBLIC ${GLFW3_LIBRARY_DIRS})
target_link_libraries(gui PUBLIC OpenGL::GL ${GLFW3_LIBRARIES})
target_compile_definitions(gui PUBLIC -DIMGUI_IMPL_OPENGL_LOADER_GL3W)

# macOS-specific: Add CoreFoundation framework
if(APPLE)
	target_link_libraries(gui PUBLIC "-framework CoreFoundation")
endif()

add_executable(mmlgui-rng
	src/main.cpp
	src/window.cpp
	src/main_window.cpp
	src/editor_window.cpp
	src/export_window.cpp
	src/pcm_tool_window.cpp
	src/song_manager.cpp
	src/track_info.cpp
	src/track_view_window.cpp
	src/track_list_window.cpp
	src/audio_manager.cpp
	src/emu_player.cpp
	src/config_window.cpp
	src/dmf_importer.cpp
	src/miniz.c
	${EMBEDDED_MDSDRV_SRC})

target_link_libraries(mmlgui-rng PRIVATE ctrmml gui vgm-utils vgm-audio vgm-emu)
target_compile_definitions(mmlgui-rng PRIVATE -DLOCAL_LIBVGM)

# Ensure mdsdrv.bin is built before building mmlgui-rng
# The embedded source file will be generated automatically as a dependency of the source file
add_dependencies(mmlgui-rng mdsdrv_bin clownassembler_asm68k_bin)

if(CPPUNIT_FOUND)
	add_executable(mmlgui_unittest
		src/track_info.cpp
		src/unittest/test_track_info.cpp
		src/unittest/main.cpp)
	target_link_libraries(mmlgui_unittest ctrmml)
	target_link_libraries(mmlgui_unittest ${CPPUNIT_LIBRARIES})
	enable_testing()
	add_test(NAME run_mmlgui_unittest COMMAND mmlgui_unittest WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()
