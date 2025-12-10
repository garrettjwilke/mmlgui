#ifndef EXPORT_WINDOW_H
#define EXPORT_WINDOW_H

#include "window.h"
#include "addons/imguifilesystem/imguifilesystem.h"
#include <string>

class Export_Window : public Window
{
	public:
		Export_Window();
		void display() override;

	private:
		char bgm_path[1024];
		char sfx_path[1024];
		char output_path[1024];
		char seq_filename[256];
		char pcm_filename[256];
		char header_filename[256];
		
		std::string status_message;
		
		void run_export();

		ImGuiFs::Dialog fs;
		bool browse_bgm;
		bool browse_sfx;
		bool browse_output;
};

#endif //EXPORT_WINDOW_H

