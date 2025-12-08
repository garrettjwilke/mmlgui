#include "imgui.h"
#include "main_window.h"
#include "editor_window.h"
#include "config_window.h"
#include "export_window.h"
#include "audio_manager.h"

#include <iostream>
#include <csignal>
#include <fstream>
#include <string>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#endif

//=====================================================================
static const char* version_string = "v0.1";

static const char* gpl_string =
	"This program is free software; you can redistribute it and/or\n"
	"modify it under the terms of the GNU General Public License\n"
	// TODO: Need to clarify the license of libvgm first.
	//"as published by the Free Software Foundation; either version 2\n"
	//"of the License, or (at your option) any later version.\n"
	"version 2 as published by the Free Software Foundation.\n"
	"\n"
	"This program is distributed in the hope that it will be useful,\n"
	"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
	"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n"
	"GNU General Public License for more details.\n"
	"\n"
	"You should have received a copy of the GNU General Public License\n"
	"along with this program; if not, write to the Free Software\n"
	"Foundation Inc., 51 Franklin Street, Fifth Floor, Boston, MA\n"
	"02110-1301, USA.";
//=====================================================================

#ifndef IMGUI_DISABLE_DEMO_WINDOWS
static bool debug_imgui_demo_windows = false;
#endif
#ifndef IMGUI_DISABLE_METRICS_WINDOW
static bool debug_imgui_metrics = false;
#endif
static bool debug_state_window = false;
static bool debug_audio_window = false;
static bool debug_ui_window = false;
static int theme_selection = 0; // 0 = Dark, 1 = Light (shared across UI settings)

// Config file path
static std::string get_config_dir()
{
#ifdef _WIN32
	char appdata_path[MAX_PATH];
	if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appdata_path)))
	{
		std::string path = std::string(appdata_path) + "\\mmlgui-rng";
		CreateDirectoryA(path.c_str(), NULL);  // Returns 0 on success or if directory exists
		return path;
	}
	return "";
#else
	const char* home = getenv("HOME");
	if (!home)
	{
		struct passwd* pw = getpwuid(getuid());
		home = pw ? pw->pw_dir : nullptr;
	}
	if (home)
	{
		std::string config_base = std::string(home) + "/.config";
		mkdir(config_base.c_str(), 0755);  // Create .config if it doesn't exist (ignore errors)
		std::string path = config_base + "/mmlgui-rng";
		mkdir(path.c_str(), 0755);  // Create mmlgui-rng directory (ignore errors if exists)
		return path;
	}
	return "";
#endif
}

static std::string get_config_file_path()
{
	std::string config_dir = get_config_dir();
	if (config_dir.empty())
		return "";
#ifdef _WIN32
	return config_dir + "\\ui_settings.ini";
#else
	return config_dir + "/ui_settings.ini";
#endif
}

void Main_Window::load_ui_settings()
{
	std::string config_file = get_config_file_path();
	if (config_file.empty())
		return;
	
	std::ifstream file(config_file);
	if (!file.is_open())
		return;
	
	std::string line;
	while (std::getline(file, line))
	{
		if (line.find("theme=") == 0)
		{
			theme_selection = std::stoi(line.substr(6));
		}
		else if (line.find("ui_scale=") == 0)
		{
			float scale = std::stof(line.substr(9));
			ImGui::GetIO().FontGlobalScale = scale;
		}
	}
	file.close();
	
	// Apply theme
	if (theme_selection == 1)
	{
		ImGui::StyleColorsLight();
		main_window.update_all_editor_palettes(true);
	}
	else
	{
		ImGui::StyleColorsDark();
		main_window.update_all_editor_palettes(false);
	}
}

static void save_ui_settings()
{
	std::string config_file = get_config_file_path();
	if (config_file.empty())
		return;
	
	std::ofstream file(config_file);
	if (!file.is_open())
		return;
	
	file << "theme=" << theme_selection << "\n";
	file << "ui_scale=" << ImGui::GetIO().FontGlobalScale << "\n";
	file.close();
}

static void debug_menu()
{
#ifndef IMGUI_DISABLE_METRICS_WINDOW
	ImGui::MenuItem("ImGui metrics", NULL, &debug_imgui_metrics);
#endif
#ifndef IMGUI_DISABLE_DEMO_WINDOWS
	ImGui::MenuItem("ImGui demo windows", NULL, &debug_imgui_demo_windows);
#endif
	ImGui::MenuItem("Select audio device", NULL, &debug_audio_window);
	ImGui::MenuItem("Display dump state", NULL, &debug_state_window);
	ImGui::MenuItem("UI settings", NULL, &debug_ui_window);
	if (ImGui::MenuItem("Quit"))
	{
		// if ctrl+shift was held, stimulate a segfault
		if(ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyAlt)
			__builtin_trap();
		// if ctrl was held, raise a signal (usually that won't happen)
		if(ImGui::GetIO().KeyCtrl)
			std::raise(SIGFPE);
		// otherwise we quit normally
		else
			main_window.close_request_all();
	}
	else if (ImGui::IsItemHovered())
		ImGui::SetTooltip("hold ctrl to make a crash dump\n");
	ImGui::EndMenu();
}

static void debug_window()
{
#ifndef IMGUI_DISABLE_METRICS_WINDOW
	if(debug_imgui_metrics)
		ImGui::ShowMetricsWindow(&debug_imgui_metrics);
#endif
#ifndef IMGUI_DISABLE_DEMO_WINDOWS
	if(debug_imgui_demo_windows)
		ImGui::ShowDemoWindow(&debug_imgui_demo_windows);
#endif
	if(debug_state_window)
	{
		std::string debug_state = main_window.dump_state_all();
		ImGui::SetNextWindowPos(ImVec2(500, 400), ImGuiCond_Once);
		ImGui::Begin("Debug state", &debug_state_window);
		if (ImGui::Button("copy to clipboard"))
			ImGui::SetClipboardText(debug_state.c_str());
		ImGui::BeginChild("debug_log", ImGui::GetContentRegionAvail(), false, ImGuiWindowFlags_HorizontalScrollbar);
		ImGui::TextUnformatted(debug_state.c_str(), debug_state.c_str()+debug_state.size());
		ImGui::EndChild();
		ImGui::End();
	}
	if(debug_audio_window)
	{
		ImGui::Begin("Select Audio Device", &debug_audio_window, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
		auto& am = Audio_Manager::get();
		auto driver_list = am.get_driver_list();
		auto driver = am.get_driver();
		if (ImGui::ListBoxHeader("Audio driver", 5))
		{
			if(ImGui::Selectable("Default", driver == -1))
			{
				am.set_driver(-1, -1);
			}
			for(auto && i : driver_list)
			{
				if(ImGui::Selectable(i.second.second.c_str(), driver == i.first))
				{
					am.set_driver(i.first, -1);
				}
			}
			ImGui::ListBoxFooter();
		}

		auto device_list = am.get_device_list();
		auto device = am.get_device();
		if (ImGui::ListBoxHeader("Audio device", 5))
		{
			if(ImGui::Selectable("Default", device == -1))
			{
				am.set_device(-1);
			}
			for(auto && i : device_list)
			{
				if(ImGui::Selectable(i.second.c_str(), device == i.first))
				{
					am.set_device(i.first);
				}
			}
			ImGui::ListBoxFooter();
		}
		ImGui::End();
	}
	if(debug_ui_window)
	{
		ImGui::Begin("UI settings", &debug_ui_window, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);

		ImGui::DragFloat("UI scaling", &ImGui::GetIO().FontGlobalScale, 0.005f, 0.3f, 2.0f, "%.2f");

		ImGui::Separator();
		ImGui::Text("Theme:");
		if (ImGui::RadioButton("Dark", theme_selection == 0))
		{
			theme_selection = 0;
			ImGui::StyleColorsDark();
			main_window.update_all_editor_palettes(false);
			save_ui_settings();
		}
		ImGui::SameLine();
		if (ImGui::RadioButton("Light", theme_selection == 1))
		{
			theme_selection = 1;
			ImGui::StyleColorsLight();
			main_window.update_all_editor_palettes(true);
			save_ui_settings();
		}
		
		// Save UI scale when it changes
		static float last_scale = ImGui::GetIO().FontGlobalScale;
		if (last_scale != ImGui::GetIO().FontGlobalScale)
		{
			last_scale = ImGui::GetIO().FontGlobalScale;
			save_ui_settings();
		}

		ImGui::End();
	}
}

//=====================================================================
FPS_Overlay::FPS_Overlay()
{
	type = WT_FPS_OVERLAY;
}

void FPS_Overlay::display()
{
	const float DISTANCE = 10.0f;
	static int corner = 0;
	ImGuiIO& io = ImGui::GetIO();
	if (corner != -1)
	{
		ImVec2 window_pos = ImVec2((corner & 1) ? io.DisplaySize.x - DISTANCE : DISTANCE, (corner & 2) ? io.DisplaySize.y - DISTANCE : DISTANCE);
		ImVec2 window_pos_pivot = ImVec2((corner & 1) ? 1.0f : 0.0f, (corner & 2) ? 1.0f : 0.0f);
		ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
	}
	ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background
	if (ImGui::Begin("overlay", &active, (corner != -1 ? ImGuiWindowFlags_NoMove : 0) | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
	{
		ImGui::Text("mmlgui (%s)", version_string);
		ImGui::Separator();
		ImGui::Text("FPS: %.2f", ImGui::GetIO().Framerate);
		if (ImGui::BeginPopupContextWindow())
		{
			if (ImGui::BeginMenu("Debug"))
				debug_menu();
			if (ImGui::BeginMenu("Overlay"))
			{
				if (ImGui::MenuItem("Custom",       NULL, corner == -1)) corner = -1;
				if (ImGui::MenuItem("Top-left",     NULL, corner == 0)) corner = 0;
				if (ImGui::MenuItem("Top-right",    NULL, corner == 1)) corner = 1;
				if (ImGui::MenuItem("Bottom-left",  NULL, corner == 2)) corner = 2;
				if (ImGui::MenuItem("Bottom-right", NULL, corner == 3)) corner = 3;
				if (ImGui::MenuItem("Close")) active = false;
				ImGui::EndMenu();
			}
			ImGui::EndPopup();
		}
	}
	ImGui::End();
}

//=====================================================================
About_Window::About_Window()
{
	type = WT_ABOUT;
}
void About_Window::display()
{
	std::string window_id;
	window_id = "About mmlgui##" + std::to_string(id);

	ImGui::Begin(window_id.c_str(), &active, ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::Text("MML (Music Macro Language) editor and player.");
	ImGui::Text("%s", version_string);
	ImGui::Text("Copyright 2020-2021 Ian Karlsson.");
	ImGui::Separator();
	ImGui::Text("Source code repository and issue reporting:");
	ImGui::Text("https://github.com/superctr/mmlgui");
	ImGui::Separator();
	ImGui::BeginChild("credits", ImVec2(500, 300), false);
	ImGui::Text("%s", gpl_string);
	ImGui::Separator();
	ImGui::Text("This program uses the following libraries:");
	ImGui::BulletText("ctrmml\nBy Ian Karlsson\nGPLv2 or later\nhttps://github.com/superctr/ctrmml");
	ImGui::BulletText("Dear ImGui\nBy Omar Cornut, et al.\nMIT license\nhttps://github.com/ocornut/imgui");
	ImGui::BulletText("glfw\nBy Marcus Geelnard & Camilla Löwy\nzlib / libpng license\nhttps://glfw.org");
	ImGui::BulletText("ImGuiColorTextEdit\nBy BalazsJako\nMIT license\nhttps://github.com/superctr/ImGuiColorTextEdit");
	ImGui::BulletText("libvgm\nBy Valley Bell, et al.\nGPLv2\nhttps://github.com/ValleyBell/libvgm");
	ImGui::EndChild();
	ImGui::End();
}

//=====================================================================
Main_Window::Main_Window()
	: show_about(false)
	, show_config(false)
	, show_export(false)
{
	children.push_back(std::make_shared<FPS_Overlay>());
	children.push_back(std::make_shared<Editor_Window>());
}

void Main_Window::display()
{
	if (ImGui::BeginPopupContextVoid())
	{
		bool overlay_active = find_child(WT_FPS_OVERLAY) != children.end();
		if (ImGui::MenuItem("Show FPS Overlay", nullptr, overlay_active))
		{
			if(overlay_active)
			{
				find_child(WT_FPS_OVERLAY)->get()->close();
			}
			else
			{
				children.push_back(std::make_shared<FPS_Overlay>());
			}
		}
		if (ImGui::MenuItem("UI Settings...", nullptr, nullptr))
		{
			debug_ui_window = true;
		}
		if (ImGui::MenuItem("New MML...", nullptr, nullptr))
		{
			children.push_back(std::make_shared<Editor_Window>());
		}
		if (ImGui::MenuItem("mdslink export...", nullptr, nullptr))
		{
			show_export_window();
		}
		if (ImGui::MenuItem("About...", nullptr, nullptr))
		{
			show_about_window();
		}
		ImGui::EndPopup();
	}
	if (show_about)
	{
		show_about = false;
		bool overlay_active = find_child(WT_ABOUT) != children.end();
		if(!overlay_active)
		{
			children.push_back(std::make_shared<About_Window>());
		}
	}
	if (show_config)
	{
		show_config = false;
		bool overlay_active = find_child(WT_CONFIG) != children.end();
		if(!overlay_active)
		{
			children.push_back(std::make_shared<Config_Window>());
		}
	}
	if (show_export)
	{
		show_export = false;
		bool overlay_active = find_child(WT_EXPORT) != children.end();
		if(!overlay_active)
		{
			children.push_back(std::make_shared<Export_Window>());
		}
	}
	debug_window();
}

void Main_Window::show_about_window()
{
	show_about = true;
}

void Main_Window::show_config_window()
{
	show_config = true;
}

void Main_Window::show_export_window()
{
	show_export = true;
}

void Main_Window::show_ui_settings_window()
{
	debug_ui_window = true;
}

void Main_Window::update_all_editor_palettes(bool light_mode)
{
	for(auto i = children.begin(); i != children.end(); i++)
	{
		Editor_Window* editor = dynamic_cast<Editor_Window*>(i->get());
		if(editor)
			editor->set_editor_palette(light_mode);
	}
}

bool Main_Window::is_light_theme() const
{
	return theme_selection == 1;
}

