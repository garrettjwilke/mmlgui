#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include "window.h"

//! FPS/version overlay
class FPS_Overlay : public Window
{
	public:
		FPS_Overlay();
		void display() override;
};

//! About window
class About_Window : public Window
{
	public:
		About_Window();
		void display() override;
};

//! Main window container
class Main_Window : public Window
{
	public:
		Main_Window();
		void display() override;

		void show_about_window();
		void show_config_window();
		void show_ui_settings_window();
		void update_all_editor_palettes(bool light_mode);
		bool is_light_theme() const;
		static void load_ui_settings();

	private:
		bool show_about;
		bool show_config;
};

extern Main_Window main_window;

#endif //MAIN_WINDOW_H
