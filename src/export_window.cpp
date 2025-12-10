#include "export_window.h"
#include "imgui.h"
#include "platform/mdsdrv.h"
#include "song.h"
#include "mml_input.h"
#include "riff.h"
#include "stringf.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;

Export_Window::Export_Window() : Window(), fs(true, false, true)
{
	type = WT_EXPORT;
	std::string cwd = fs::current_path().string();
	strncpy(bgm_path, "musicdata", sizeof(bgm_path) - 1);
	strncpy(sfx_path, "sfxdata", sizeof(sfx_path) - 1);
	strncpy(output_path, cwd.c_str(), sizeof(output_path) - 1);
	strncpy(seq_filename, "mdsseq.bin", sizeof(seq_filename) - 1);
	strncpy(pcm_filename, "mdspcm.bin", sizeof(pcm_filename) - 1);
	strncpy(header_filename, "mdsseq.h", sizeof(header_filename) - 1);
	status_message = "Ready";
	browse_bgm = false;
	browse_sfx = false;
	browse_output = false;
}

void Export_Window::display()
{
	if (!active) return;

	ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("mdslink export", &active))
	{
		ImGui::InputText("BGM MML Directory", bgm_path, sizeof(bgm_path));
		ImGui::SameLine();
		bool trigger_bgm = ImGui::Button("...##bgm");

		ImGui::InputText("SFX MML Directory", sfx_path, sizeof(sfx_path));
		ImGui::SameLine();
		bool trigger_sfx = ImGui::Button("...##sfx");

		ImGui::InputText("Output Directory", output_path, sizeof(output_path));
		ImGui::SameLine();
		bool trigger_output = ImGui::Button("...##output");
		
		if (trigger_bgm) {
			browse_bgm = true;
			browse_sfx = false;
			browse_output = false;
		}
		if (trigger_sfx) {
			browse_bgm = false;
			browse_sfx = true;
			browse_output = false;
		}
		if (trigger_output) {
			browse_bgm = false;
			browse_sfx = false;
			browse_output = true;
		}

		if (browse_bgm) {
			const char* path = fs.chooseFolderDialog(trigger_bgm, bgm_path);
			if (strlen(path) > 0) {
				strncpy(bgm_path, path, sizeof(bgm_path) - 1);
				browse_bgm = false;
			} else if (fs.hasUserJustCancelledDialog()) {
				browse_bgm = false;
			}
		}
		else if (browse_sfx) {
			const char* path = fs.chooseFolderDialog(trigger_sfx, sfx_path);
			if (strlen(path) > 0) {
				strncpy(sfx_path, path, sizeof(sfx_path) - 1);
				browse_sfx = false;
			} else if (fs.hasUserJustCancelledDialog()) {
				browse_sfx = false;
			}
		}
		else if (browse_output) {
			const char* path = fs.chooseFolderDialog(trigger_output, output_path);
			if (strlen(path) > 0) {
				strncpy(output_path, path, sizeof(output_path) - 1);
				browse_output = false;
			} else if (fs.hasUserJustCancelledDialog()) {
				browse_output = false;
			}
		}

		ImGui::Separator();
		ImGui::InputText("Sequence Filename", seq_filename, sizeof(seq_filename));
		ImGui::InputText("PCM Filename", pcm_filename, sizeof(pcm_filename));
		ImGui::InputText("Header Filename", header_filename, sizeof(header_filename));
		ImGui::Separator();
		
		if (ImGui::Button("Export"))
		{
			run_export();
		}
		
		ImGui::Separator();
		ImGui::Text("Output:");
		ImGui::BeginChild("export_output", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false, ImGuiWindowFlags_HorizontalScrollbar);
		ImGui::TextUnformatted(status_message.c_str());
		ImGui::EndChild();
	}
	ImGui::End();
}

static Song convert_file(const std::string& filename, std::string& log_output)
{
	Song song;
	MML_Input input = MML_Input(&song);
	input.open_file(filename.c_str());
	// Validate logic from mdslink.cpp
	// We just return the song, validation happens implicitly or we can add it here if needed for logging
	return song;
}

// Helper to match string ending
static bool ends_with(const std::string& str, const std::string& suffix) {
    if (str.length() < suffix.length()) return false;
    return str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0;
}

void Export_Window::run_export()
{
	status_message = "Exporting...";
	std::vector<std::string> input_files;
	
	try {
		// Search BGM directory
		if (fs::exists(bgm_path) && fs::is_directory(bgm_path)) {
			for (const auto& entry : fs::recursive_directory_iterator(bgm_path)) {
				if (entry.is_regular_file()) {
					std::string path = entry.path().string();
					std::string ext = entry.path().extension().string();
					// Check for .mml or .mds extension (case insensitive)
					if (iequal(ext, ".mml") || iequal(ext, ".mds")) {
						input_files.push_back(path);
					}
				}
			}
		} else if (strlen(bgm_path) > 0) {
			status_message = "Invalid BGM directory: " + std::string(bgm_path);
			return;
		}

		// Search SFX directory
		if (fs::exists(sfx_path) && fs::is_directory(sfx_path)) {
			for (const auto& entry : fs::recursive_directory_iterator(sfx_path)) {
				if (entry.is_regular_file()) {
					std::string path = entry.path().string();
					std::string ext = entry.path().extension().string();
					// Check for .mml or .mds extension (case insensitive)
					if (iequal(ext, ".mml") || iequal(ext, ".mds")) {
						input_files.push_back(path);
					}
				}
			}
		} else if (strlen(sfx_path) > 0) {
			status_message = "Invalid SFX directory: " + std::string(sfx_path);
			return;
		}
		
		if (input_files.empty()) {
			status_message = "No .mml or .mds files found in BGM or SFX directories.";
			return;
		}

		MDSDRV_Linker linker;
		std::string log;
		log = "Processing " + std::to_string(input_files.size()) + " file(s)...\n\n";

		for (size_t i = 0; i < input_files.size(); ++i) {
			const auto& file = input_files[i];
			std::string ext = fs::path(file).extension().string();
			std::string filename_stem = fs::path(file).stem().string(); // Equivalent to get_filename in mdslink
			
			log += "[" + std::to_string(i+1) + "/" + std::to_string(input_files.size()) + "] " + file + "\n";

			RIFF mds(0);
			
			if (iequal(ext, ".mds")) {
				std::ifstream in(file, std::ios::binary | std::ios::ate);
				if (in) {
					auto size = in.tellg();
					std::vector<uint8_t> data(size);
					in.seekg(0);
					if (in.read((char*)data.data(), size)) {
						mds = RIFF(data);
					} else {
						status_message = "Failed to read " + file;
						return;
					}
				} else {
					status_message = "Failed to open " + file;
					return;
				}
			} else {
				// MML
				Song song = convert_file(file, log);
				MDSDRV_Converter converter(song);
				mds = converter.get_mds();
			}
			
			linker.add_song(mds, filename_stem);
		}
		
		log += "\n";

		fs::path out_dir(output_path);
		if (!fs::exists(out_dir)) {
			fs::create_directories(out_dir);
		}

		// Write seq
		if (strlen(seq_filename) > 0) {
			fs::path p = out_dir / seq_filename;
			log += "Writing " + p.string() + "...\n";
			auto bytes = linker.get_seq_data();
			std::ofstream out(p, std::ios::binary);
			out.write((char*)bytes.data(), bytes.size());
			log += "  Wrote " + std::to_string(bytes.size()) + " bytes\n";
		}

		// Write pcm
		if (strlen(pcm_filename) > 0) {
			fs::path p = out_dir / pcm_filename;
			log += "Writing " + p.string() + "...\n";
			auto bytes = linker.get_pcm_data();
			std::ofstream out(p, std::ios::binary);
			out.write((char*)bytes.data(), bytes.size());
			log += "  Wrote " + std::to_string(bytes.size()) + " bytes\n";
			log += "\n" + linker.get_statistics();
		}

		// Write header
		if (strlen(header_filename) > 0) {
			fs::path p = out_dir / header_filename;
			log += "Writing " + p.string() + "...\n";
			auto bytes = linker.get_c_header();
			std::ofstream out(p);
			out.write((char*)bytes.data(), bytes.size());
			log += "  Wrote " + std::to_string(bytes.size()) + " bytes\n";
		}
		
		status_message = "Export Successful!\n\n" + log;

	} catch (const std::exception& e) {
		status_message = std::string("Error: ") + e.what();
	} catch (...) {
		status_message = "Unknown error occurred.";
	}
}

