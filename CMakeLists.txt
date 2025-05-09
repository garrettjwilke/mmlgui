cmake_minimum_required(VERSION 3.11)
project(mmlgui LANGUAGES C CXX)

find_package(PkgConfig REQUIRED)
find_package(OpenGL REQUIRED)

pkg_check_modules(GLFW3 REQUIRED glfw3)
pkg_check_modules(CPPUNIT cppunit)

if(MINGW)
  set(OPENGL_gl_LIBRARY /mingw64/lib/libopengl32.a)
  set(OPENGL_INCLUDE_DIR /mingw64/include)
endif()

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

add_subdirectory(ctrmml)
add_subdirectory(libvgm)

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

add_executable(mmlgui
	src/main.cpp
	src/window.cpp
	src/main_window.cpp
	src/editor_window.cpp
	src/song_manager.cpp
	src/track_info.cpp
	src/track_view_window.cpp
	src/track_list_window.cpp
	src/audio_manager.cpp
	src/emu_player.cpp
	src/config_window.cpp
	src/dmf_importer.cpp
	src/miniz.c)

target_link_libraries(mmlgui PRIVATE ctrmml gui vgm-utils vgm-audio vgm-emu)
target_compile_definitions(mmlgui PRIVATE -DLOCAL_LIBVGM)

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
