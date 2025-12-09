cmake_minimum_required(VERSION 3.11)
project(mmlgui LANGUAGES C CXX)

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

# Build mdsdrv.bin from MDSDRV directory (must be built before mmlgui-rng)
# Determine the C compiler to use for MDSDRV build
# On Windows/MSYS2, use gcc; otherwise try to use CMAKE_C_COMPILER or default to clang
if(WIN32 OR MINGW)
	set(MDSDRV_CC "gcc")
elseif(CMAKE_C_COMPILER)
	# Use CMake's detected C compiler
	get_filename_component(MDSDRV_CC_NAME ${CMAKE_C_COMPILER} NAME)
	if(MDSDRV_CC_NAME MATCHES "clang")
		set(MDSDRV_CC "clang")
	else()
		set(MDSDRV_CC "gcc")
	endif()
else()
	# Default to clang on Unix-like systems
	set(MDSDRV_CC "clang")
endif()

# Pass CC as both environment variable and make variable to ensure sub-makes inherit it
add_custom_target(mdsdrv_bin
	COMMAND ${CMAKE_COMMAND} -E env "CC=${MDSDRV_CC}" ${CMAKE_MAKE_PROGRAM} -C ${CMAKE_SOURCE_DIR}/MDSDRV CC=${MDSDRV_CC} mdsdrv
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/MDSDRV
	COMMENT "Building mdsdrv.bin (using ${MDSDRV_CC})"
	BYPRODUCTS ${CMAKE_SOURCE_DIR}/MDSDRV/out/mdsdrv.bin
)

# Generate embedded mdsdrv.bin source file
set(MDSDRV_BIN_PATH ${CMAKE_SOURCE_DIR}/MDSDRV/out/mdsdrv.bin)
set(EMBEDDED_MDSDRV_SRC ${CMAKE_BINARY_DIR}/embedded_mdsdrv_bin.cpp)

# Create a custom command that depends on mdsdrv_bin and generates the embedded source
add_custom_command(
	OUTPUT ${EMBEDDED_MDSDRV_SRC}
	COMMAND ${CMAKE_COMMAND} 
		-DINPUT_FILE=${MDSDRV_BIN_PATH} 
		-DOUTPUT_FILE=${EMBEDDED_MDSDRV_SRC} 
		-DVARIABLE_NAME=embedded_mdsdrv_bin_data 
		-P ${CMAKE_CURRENT_SOURCE_DIR}/embed_binary.cmake
	DEPENDS mdsdrv_bin
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
add_dependencies(mmlgui-rng mdsdrv_bin)

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
