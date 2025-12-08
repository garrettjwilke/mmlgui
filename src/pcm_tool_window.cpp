#include "pcm_tool_window.h"
#include "imgui.h"
#include "main_window.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <sys/stat.h>
#include "stringf.h"
#include "audio_manager.h"

// Simple Audio Stream for Preview
class PCM_Preview_Stream : public Audio_Stream
{
public:
    PCM_Preview_Stream(const std::vector<short>& data, int start, int end, int rate, bool loop, int* position_ptr)
        : data(data), start(start), end(end), rate(rate), loop(loop), pos(0.0), step(0.0), position_ptr(position_ptr)
    {
        if (this->start < 0) this->start = 0;
        if (this->end > (int)this->data.size()) this->end = (int)this->data.size();
        if (this->start >= this->end) {
             this->start = 0;
             this->end = 0;
        }
    }

    void setup_stream(uint32_t output_rate) override
    {
        if (output_rate > 0 && rate > 0)
            step = (double)rate / (double)output_rate;
        else
            step = 1.0;
        pos = 0.0;
    }

    int get_sample(WAVE_32BS* output, int count, int channels) override
    {
        if (start >= end) {
            // Fill with silence if empty
             for (int i = 0; i < count; ++i) {
                output[i].L = 0;
                output[i].R = 0;
            }
            if (!finished) set_finished(true);
            return 0;
        }

        for (int i = 0; i < count; ++i)
        {
            // Update current playback position (in original sample indices) before processing
            if (position_ptr) {
                int current_idx = start + (int)pos;
                // Handle looping
                if (loop && current_idx >= end) {
                    current_idx = start + ((current_idx - start) % (end - start));
                }
                // Clamp to valid range
                if (current_idx < start) current_idx = start;
                if (current_idx > end) current_idx = end;
                *position_ptr = current_idx;
            }
            
            int idx0 = start + (int)pos;
            int idx1 = idx0 + 1;
            
            if (idx0 >= end)
            {
                if (loop)
                {
                     pos -= (end - start);
                     idx0 = start + (int)pos;
                     idx1 = idx0 + 1;
                }
                else
                {
                    // Silence rest of buffer
                    for (; i < count; ++i) {
                        output[i].L = 0;
                        output[i].R = 0;
                    }
                    if (!finished) set_finished(true);
                    return 0;
                }
            }
            
            if (idx1 >= end)
            {
                 if (loop) idx1 = start; 
                 else idx1 = end - 1; // Clamp to last valid sample
            }

            // Ensure indices are within valid data bounds (safety check)
            if (idx0 < 0) idx0 = 0;
            if (idx1 < 0) idx1 = 0;
            if (idx0 >= (int)data.size()) idx0 = (int)data.size() - 1;
            if (idx1 >= (int)data.size()) idx1 = (int)data.size() - 1;

            double frac = pos - (int)pos;
            short s0 = data[idx0];
            short s1 = data[idx1];
            
            // Linear interpolation
            int32_t val = (int32_t)(s0 + (s1 - s0) * frac);

            // Mixer expects 8.24 fixed point or similar scaling (shifted down by 8 in callback)
            output[i].L = val << 8;
            output[i].R = val << 8; 

            pos += step;
        }
        
        return 1;
    }

    void stop_stream() override
    {
    }

private:
    std::vector<short> data; // Copy of data for thread safety
    int start;
    int end;
    int rate;
    bool loop;
    double pos;
    double step;
    int* position_ptr; // Pointer to update current playback position
};

PCM_Tool_Window::PCM_Tool_Window() : Window(), fs(true, false, true), browse_open(false), browse_save(false)
{
    type = WT_PCM_TOOL;
    sample_rate = 0;
    channels = 0;
    start_point = 0;
    end_point = 0;
    preview_loop = false;
    double_speed = false;
    current_playback_position = -1;
    zoom_enabled = false;
    zoom_point = 0; // Default to start point
    zoom_level = 1.0f;
    zoom_window_samples = 1000; // Default zoom window
    slice_enabled = false;
    num_slices = 2;
    status_message = "Ready";
    memset(input_path, 0, sizeof(input_path));
}

float PCM_Tool_Window::WaveformGetter(void* data, int idx)
{
    const std::vector<short>* pcm = (const std::vector<short>*)data;
    if (idx < 0 || idx >= (int)pcm->size()) return 0.0f;
    return (float)(*pcm)[idx] / 32768.0f;
}

void PCM_Tool_Window::display()
{
    // Safety check: if window became inactive unexpectedly, stop preview
    if (!active)
    {
        stop_preview();
        return;
    }

    // Handle close request flow
    if (get_close_request() == Window::CLOSE_IN_PROGRESS && !modal_open)
        show_close_warning();

    if (get_close_request() == Window::CLOSE_OK)
    {
        cleanup();
        active = false;
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    std::string window_title = "PCM Tool";
    if (!current_filename.empty()) {
        window_title += " - " + current_filename;
    }
    window_title += "###PCMTool" + std::to_string(id); // ensure unique ID per instance
    
    // Use local variable to detect X button click
    bool window_open = active;
    if (ImGui::Begin(window_title.c_str(), &window_open))
    {
        // If user clicked X button (window_open became false) and we haven't started close request, trigger it
        if (!window_open && get_close_request() == Window::NO_CLOSE_REQUEST)
        {
            window_open = true; // Keep window open to show dialog
            active = true; // Ensure active stays true
            close_request();
        }
        else
        {
            active = window_open; // Sync with ImGui state
        }
        bool load_clicked = ImGui::Button("Load WAV...");
        if (load_clicked)
        {
            browse_open = true;
            browse_save = false;
        }
        ImGui::SameLine();
        ImGui::Text("%s", current_filename.c_str());

        if (browse_open)
        {
            // Center the dialog
            ImVec2 center = ImGui::GetIO().DisplaySize;
            center.x *= 0.5f;
            center.y *= 0.5f;
            ImVec2 size(600, 400);
            ImVec2 pos = ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f);

            // Ensure we don't pass browse_open directly if the dialog expects a trigger button press only once
            const char* path = fs.chooseFileDialog(load_clicked, input_path, ".wav;.mp3", "Load Audio", size, pos);
            if (strlen(path) > 0)
            {
                load_file(path);
                browse_open = false;
            }
            else if (fs.hasUserJustCancelledDialog())
            {
                browse_open = false;
            }
        }

        if (pcm_data.size() > 0)
        {
            ImGui::Separator();
            ImGui::Text("Sample Rate: %d Hz", sample_rate);
            ImGui::SameLine();
            ImGui::Text("Channels: %d", channels);
            ImGui::SameLine();
            ImGui::Text("Length: %d samples", (int)pcm_data.size());

            // Zoom controls
            ImGui::Checkbox("Zoom", &zoom_enabled);
            if (zoom_enabled) {
                ImGui::SameLine();
                if (ImGui::RadioButton("Start", zoom_point == 0)) zoom_point = 0;
                ImGui::SameLine();
                if (ImGui::RadioButton("End", zoom_point == 1)) zoom_point = 1;
                ImGui::SameLine();
                if (ImGui::Button("Zoom In")) {
                    zoom_window_samples = (int)(zoom_window_samples * 0.5f);
                    if (zoom_window_samples < 10) zoom_window_samples = 10;
                }
                ImGui::SameLine();
                if (ImGui::Button("Zoom Out")) {
                    zoom_window_samples = (int)(zoom_window_samples * 2.0f);
                    if (zoom_window_samples > (int)pcm_data.size()) zoom_window_samples = (int)pcm_data.size();
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset")) {
                    zoom_window_samples = 1000;
                }
            }

            // Waveform display with visible bounding box
            float plot_height = 150.0f;
            ImVec2 content_region = ImGui::GetContentRegionAvail();
            float plot_width = content_region.x;
            
            // Define margins for markers and padding
            float margin_x = 15.0f; // Horizontal margin to ensure markers are visible
            float margin_y = 20.0f; // Vertical margin for easier marker grabbing at top/bottom
            
            // Define the visible box bounds (with margins for markers)
            ImVec2 box_min = ImGui::GetCursorScreenPos();
            box_min.x += margin_x;
            box_min.y += margin_y;
            ImVec2 box_max = ImVec2(box_min.x + plot_width - margin_x * 2.0f, box_min.y + plot_height);
            
            // Draw the bounding box
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            draw_list->AddRect(box_min, box_max, IM_COL32(200, 200, 200, 255), 0.0f, 0, 1.0f);
            
            // Position cursor for waveform plot (inside the box with padding)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + margin_x);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + margin_y);
            
            // Use the box bounds for marker calculations (markers can extend into the padding area)
            ImVec2 plot_min = ImVec2(box_min.x + 2.0f, box_min.y); // Markers can use full box height
            ImVec2 plot_max = ImVec2(box_max.x - 2.0f, box_max.y); // Markers can use full box height
            
            // Calculate zoom window if enabled
            int zoom_start_sample = 0;
            int zoom_end_sample = (int)pcm_data.size();
            int zoom_center_sample = 0;
            std::vector<short> zoom_data;
            
            if (zoom_enabled && pcm_data.size() > 0) {
                // Determine center point based on selected marker
                if (zoom_point == 0) {
                    zoom_center_sample = start_point;
                } else {
                    zoom_center_sample = end_point;
                }
                
                // Calculate zoom window around center
                int half_window = zoom_window_samples / 2;
                zoom_start_sample = zoom_center_sample - half_window;
                zoom_end_sample = zoom_center_sample + half_window;
                
                // Clamp to valid range
                if (zoom_start_sample < 0) {
                    zoom_end_sample += -zoom_start_sample;
                    zoom_start_sample = 0;
                }
                if (zoom_end_sample > (int)pcm_data.size()) {
                    zoom_start_sample -= (zoom_end_sample - (int)pcm_data.size());
                    zoom_end_sample = (int)pcm_data.size();
                    if (zoom_start_sample < 0) zoom_start_sample = 0;
                }
                
                // Extract zoom window data
                zoom_data.clear();
                for (int i = zoom_start_sample; i < zoom_end_sample; ++i) {
                    if (i >= 0 && i < (int)pcm_data.size()) {
                        zoom_data.push_back(pcm_data[i]);
                    }
                }
            }
            
            // Draw waveform inside the box (zoomed or full view)
            ImVec2 plot_size = ImVec2(plot_width - margin_x * 2.0f, plot_height);
            if (zoom_enabled && !zoom_data.empty()) {
                // Draw zoomed waveform
                ImGui::PlotLines("##Waveform", WaveformGetter, (void*)&zoom_data, (int)zoom_data.size(), 0, NULL, -1.0f, 1.0f, plot_size);
            } else {
                // Draw full waveform
                ImGui::PlotLines("##Waveform", WaveformGetter, (void*)&pcm_data, (int)pcm_data.size(), 0, NULL, -1.0f, 1.0f, plot_size);
            }
            
            // Interaction logic for drag tabs
            bool selection_changed = false;

            if (pcm_data.size() > 0)
            {
                float width = plot_max.x - plot_min.x;
                float x_step;
                float count;
                
                if (zoom_enabled && !zoom_data.empty()) {
                    // In zoom mode, map zoom window samples to display width
                    count = (float)(zoom_data.size() > 1 ? zoom_data.size() : 1);
                    x_step = width / count;
                } else {
                    // Normal mode: map all samples to display width
                    count = (float)(pcm_data.size() > 1 ? pcm_data.size() : 1);
                    x_step = width / count;
                }
                
                float handle_size = 10.0f;
                
                // Unique IDs per window to avoid ImGui ID collisions across multiple PCM windows
                std::string start_tab_id = "##start_tab_" + std::to_string(id);
                std::string end_tab_id   = "##end_tab_"   + std::to_string(id);

                // Helper to draw and handle tab interaction
                auto handle_tab = [&](int* point, bool is_top, ImU32 color, const char* id) {
                    if (*point < 0) *point = 0;
                    if (*point > (int)pcm_data.size()) *point = (int)pcm_data.size();
                    
                    // Map sample index to X position within the box bounds
                    float x;
                    if (zoom_enabled && !zoom_data.empty()) {
                        // In zoom mode: map point relative to zoom window
                        int point_in_zoom = *point - zoom_start_sample;
                        if (zoom_data.size() > 1) {
                            x = plot_min.x + point_in_zoom * x_step;
                        } else {
                            x = plot_min.x + width * 0.5f;
                        }
                    } else {
                        // Normal mode: map point to full range
                        if (pcm_data.size() > 1) {
                            x = plot_min.x + (*point) * x_step;
                        } else {
                            // Special case: only one sample, center it
                            x = plot_min.x + width * 0.5f;
                        }
                    }
                    
                    // Clamp to box bounds (markers must stay inside the visible box)
                    if (x > plot_max.x) x = plot_max.x;
                    if (x < plot_min.x) x = plot_min.x;

                    ImVec2 tab_pos = is_top ? ImVec2(x, plot_min.y) : ImVec2(x, plot_max.y);
                    
                    // Triangle tab shape
                    ImVec2 p1 = tab_pos;
                    ImVec2 p2 = ImVec2(tab_pos.x - handle_size/2, tab_pos.y + (is_top ? -handle_size : handle_size));
                    ImVec2 p3 = ImVec2(tab_pos.x + handle_size/2, tab_pos.y + (is_top ? -handle_size : handle_size));
                    
                    // Invisible button for interaction
                    // Center the button on the tab tip (p1) horizontally, and cover the triangle vertically
                    ImGui::SetCursorScreenPos(ImVec2(p2.x, is_top ? p2.y : p1.y));
                    ImGui::InvisibleButton(id, ImVec2(handle_size, handle_size));
                    
                    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0))
                    {
                        float delta_x = ImGui::GetIO().MouseDelta.x;
                        int delta_samples = (int)(delta_x / x_step);
                        if (delta_samples != 0) {
                            *point += delta_samples;
                            // Clamp again
                            if (*point < 0) *point = 0;
                            if (*point > (int)pcm_data.size()) *point = (int)pcm_data.size();
                            selection_changed = true;
                        }
                    }
                    
                    // Draw tab
                    draw_list->AddTriangleFilled(p1, p2, p3, color);
                    // Draw line
                    draw_list->AddLine(ImVec2(x, plot_min.y), ImVec2(x, plot_max.y), color, 2.0f);
                };

                // Start Point (Green) - Top Tab
                handle_tab(&start_point, true, IM_COL32(0, 255, 0, 255), start_tab_id.c_str());
                
                // End Point (Red) - Bottom Tab
                handle_tab(&end_point, false, IM_COL32(255, 0, 0, 255), end_tab_id.c_str());
                
                // Draw playback position marker (Blue) if previewing
                bool is_playing = (preview_stream && !preview_stream->get_finished());
                if (is_playing && current_playback_position >= 0) {
                    float x;
                    if (zoom_enabled && !zoom_data.empty()) {
                        // In zoom mode: map position relative to zoom window
                        int pos_in_zoom = current_playback_position - zoom_start_sample;
                        if (zoom_data.size() > 1 && pos_in_zoom >= 0 && pos_in_zoom < (int)zoom_data.size()) {
                            x = plot_min.x + pos_in_zoom * x_step;
                        } else {
                            // Position is outside zoom window, don't draw
                            x = -1;
                        }
                    } else {
                        // Normal mode
                        if (pcm_data.size() > 1) {
                            x = plot_min.x + current_playback_position * x_step;
                        } else {
                            x = plot_min.x + width * 0.5f;
                        }
                    }
                    
                    // Draw blue vertical line for playback position (if visible in zoom window)
                    if (x >= 0) {
                        // Clamp to box bounds
                        if (x > plot_max.x) x = plot_max.x;
                        if (x < plot_min.x) x = plot_min.x;
                        draw_list->AddLine(ImVec2(x, plot_min.y), ImVec2(x, plot_max.y), IM_COL32(0, 150, 255, 255), 2.0f);
                    }
                }
            }
            
            // Advance cursor past the waveform area (including padding)
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + margin_y);
            
            // Sliders for Start/End
            int max_sample = (int)pcm_data.size();
            if (end_point > max_sample) end_point = max_sample;
            if (start_point >= end_point) start_point = end_point - 1;

            if (ImGui::DragInt("Start Point", &start_point, 1.0f, 0, end_point - 1)) selection_changed = true;
            if (ImGui::DragInt("End Point", &end_point, 1.0f, start_point + 1, max_sample)) selection_changed = true;

            // If preview is playing and selection changed, restart playback from the new start
            bool is_playing = (preview_stream && !preview_stream->get_finished());
            if (selection_changed && is_playing) {
                start_preview(); // start_preview() stops the current stream first
            }
            
            ImGui::Checkbox("Loop Preview", &preview_loop);
            ImGui::SameLine();
            
            if (ImGui::Button(is_playing ? "Stop Preview" : "Preview"))
            {
                if (is_playing)
                    stop_preview();
                else
                    start_preview();
            }

            ImGui::Separator();
            ImGui::Checkbox("Double Speed", &double_speed);
            
            ImGui::Separator();
            ImGui::Checkbox("Enable Slicing", &slice_enabled);
            if (slice_enabled)
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100);
                ImGui::InputInt("Number of Slices", &num_slices, 1, 1);
                if (num_slices < 1) num_slices = 1;
                if (num_slices > 100) num_slices = 100;
            }
            
            ImGui::Separator();
            bool save_clicked = ImGui::Button("Export (17.5kHz Mono s16le)...");
            if (save_clicked)
            {
                stop_preview(); // Stop any playing preview before exporting
                browse_save = true;
                browse_open = false;
            }
            ImGui::SameLine();
            bool export_window_clicked = ImGui::Button("Export to New Window");
            if (export_window_clicked)
            {
                stop_preview(); // Stop preview before creating new window
                export_to_new_window();
            }

             if (browse_save)
            {
                // Center the dialog
                ImVec2 center = ImGui::GetIO().DisplaySize;
                center.x *= 0.5f;
                center.y *= 0.5f;
                ImVec2 size(600, 400);
                ImVec2 pos = ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f);

                const char* path = fs.saveFileDialog(save_clicked, input_path, slice_enabled ? "output.wav" : "output.wav", ".wav", slice_enabled ? "Save PCM (Base Name)" : "Save PCM", size, pos);
                if (strlen(path) > 0)
                {
                    if (slice_enabled)
                    {
                        // For slicing, we'll save multiple files, so check if any would overwrite
                        bool would_overwrite = false;
                        std::string base_path = path;
                        // Remove .wav extension if present
                        if (base_path.length() >= 4 && base_path.substr(base_path.length() - 4) == ".wav")
                        {
                            base_path = base_path.substr(0, base_path.length() - 4);
                        }
                        
                        for (int i = 1; i <= num_slices; ++i)
                        {
                            std::string slice_path = base_path + "-" + std::to_string(i) + ".wav";
                            if (ImGuiFs::FileExists(slice_path.c_str()))
                            {
                                would_overwrite = true;
                                break;
                            }
                        }
                        
                        if (would_overwrite)
                        {
                            pending_save_path = path;
                            ImGui::OpenPopup("Overwrite?");
                            browse_save = false;
                        }
                        else
                        {
                            resample_and_save_slices(path);
                            browse_save = false;
                        }
                    }
                    else
                    {
                        if (ImGuiFs::FileExists(path))
                        {
                            pending_save_path = path;
                            ImGui::OpenPopup("Overwrite?");
                            browse_save = false;
                        }
                        else
                        {
                            resample_and_save(path);
                            browse_save = false;
                        }
                    }
                }
                else if (fs.hasUserJustCancelledDialog())
                {
                    browse_save = false;
                }
            }

            if (ImGui::BeginPopupModal("Overwrite?", NULL, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("File already exists.\nOverwrite?");
                ImGui::Separator();

                if (ImGui::Button("Yes", ImVec2(120, 0)))
                {
                    if (slice_enabled)
                    {
                        resample_and_save_slices(pending_save_path.c_str());
                    }
                    else
                    {
                        resample_and_save(pending_save_path.c_str());
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SetItemDefaultFocus();
                ImGui::SameLine();
                if (ImGui::Button("No", ImVec2(120, 0)))
                {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
        
        ImGui::Separator();
        ImGui::TextWrapped("%s", status_message.c_str());
    }
    ImGui::End();
}

void PCM_Tool_Window::load_file(const char* filename)
{
    // Variables for MP3 conversion (need to be outside try block for cleanup)
    std::string filepath = filename;
    std::string temp_wav;
    bool is_mp3 = false;
    
    try {
        // Check if file is MP3 and convert to WAV if needed
        
        // Check file extension
        size_t dot_pos = filepath.find_last_of('.');
        if (dot_pos != std::string::npos) {
            std::string ext = filepath.substr(dot_pos + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == "mp3") {
                is_mp3 = true;
            }
        }
        
        // If MP3, convert to temporary WAV file
        if (is_mp3) {
            // Try to use ffmpeg to convert MP3 to WAV
            temp_wav = filepath + ".temp.wav";
            std::stringstream cmd;
            cmd << "ffmpeg -i \"" << filepath << "\" -f wav -acodec pcm_s16le -ar 44100 -ac 2 -y \"" << temp_wav << "\" 2>&1";
            
            int result = system(cmd.str().c_str());
            if (result != 0) {
                // Try with sox as fallback
                temp_wav = filepath + ".temp.wav";
                cmd.str("");
                cmd << "sox \"" << filepath << "\" -r 44100 -c 2 -b 16 \"" << temp_wav << "\" 2>&1";
                result = system(cmd.str().c_str());
                if (result != 0) {
                    status_message = "MP3 conversion failed. Please install ffmpeg or sox.";
                    return;
                }
            }
            filepath = temp_wav;
        }
        
        // Simple WAV file reader (since we can't access Wave_File private members)
        std::ifstream file(filepath, std::ios::binary);
        if (!file) {
            status_message = "Failed to open audio file";
            if (is_mp3 && !temp_wav.empty()) {
                // Clean up temp file
                remove(temp_wav.c_str());
            }
            return;
        }

        // Read RIFF header
        char riff[4];
        file.read(riff, 4);
        if (memcmp(riff, "RIFF", 4) != 0) {
            status_message = "Not a valid WAV file (RIFF header missing)";
            if (is_mp3 && !temp_wav.empty()) {
                remove(temp_wav.c_str());
            }
            return;
        }

        uint32_t file_size;
        file.read((char*)&file_size, 4);

        char wave[4];
        file.read(wave, 4);
        if (memcmp(wave, "WAVE", 4) != 0) {
            status_message = "Not a valid WAV file (WAVE header missing)";
            if (is_mp3 && !temp_wav.empty()) {
                remove(temp_wav.c_str());
            }
            return;
        }

        // Read chunks
        uint16_t num_channels = 0;
        uint32_t sample_rate_val = 0;
        uint16_t bits_per_sample = 0;
        uint16_t audio_format = 0;
        std::vector<std::vector<int16_t>> channel_data;
        bool found_fmt = false;
        bool found_data = false;

        while (file && !found_data) {
            char chunk_id[4];
            uint32_t chunk_size;
            file.read(chunk_id, 4);
            if (file.gcount() != 4) break;
            file.read((char*)&chunk_size, 4);
            if (file.gcount() != 4) break;

            if (memcmp(chunk_id, "fmt ", 4) == 0) {
                file.read((char*)&audio_format, 2);
                file.read((char*)&num_channels, 2);
                file.read((char*)&sample_rate_val, 4);
                uint32_t byte_rate;
                file.read((char*)&byte_rate, 4);
                uint16_t block_align;
                file.read((char*)&block_align, 2);
                file.read((char*)&bits_per_sample, 2);
                
                // Skip any extra format data
                if (chunk_size > 16) {
                    file.seekg(chunk_size - 16, std::ios::cur);
                }
                found_fmt = true;
            }
            else if (memcmp(chunk_id, "data", 4) == 0) {
                if (!found_fmt || num_channels == 0) {
                    status_message = "Invalid WAV format (missing format info)";
                    if (is_mp3 && !temp_wav.empty()) {
                        remove(temp_wav.c_str());
                    }
                    return;
                }

                // Only support PCM (format 1) for now
                if (audio_format != 1) {
                    status_message = "Unsupported audio format (only PCM supported)";
                    if (is_mp3 && !temp_wav.empty()) {
                        remove(temp_wav.c_str());
                    }
                    return;
                }

                channel_data.resize(num_channels);
                size_t bytes_per_sample = bits_per_sample / 8;
                size_t samples = chunk_size / (num_channels * bytes_per_sample);
                
                // Read and convert samples based on bit depth
                for (size_t i = 0; i < samples; ++i) {
                    for (int ch = 0; ch < num_channels; ++ch) {
                        int32_t sample_value = 0;
                        
                        if (bits_per_sample == 8) {
                            uint8_t sample_u8;
                            file.read((char*)&sample_u8, 1);
                            // Convert unsigned 8-bit to signed 16-bit
                            sample_value = ((int32_t)sample_u8 - 128) << 8;
                        }
                        else if (bits_per_sample == 16) {
                            int16_t sample_s16;
                            file.read((char*)&sample_s16, 2);
                            sample_value = (int32_t)sample_s16;
                        }
                        else if (bits_per_sample == 24) {
                            // Read 24-bit as 3 bytes (little-endian)
                            uint8_t bytes[3];
                            file.read((char*)bytes, 3);
                            // Sign extend to 32-bit
                            int32_t sample_24 = (int32_t)(bytes[0] | (bytes[1] << 8) | (bytes[2] << 16));
                            if (sample_24 & 0x800000) sample_24 |= 0xFF000000; // Sign extend
                            sample_value = sample_24 >> 8; // Convert to 16-bit range
                        }
                        else if (bits_per_sample == 32) {
                            int32_t sample_s32;
                            file.read((char*)&sample_s32, 4);
                            // Convert 32-bit to 16-bit range
                            sample_value = sample_s32 >> 16;
                        }
                        else {
                            status_message = "Unsupported bit depth: " + std::to_string(bits_per_sample);
                            if (is_mp3 && !temp_wav.empty()) {
                                remove(temp_wav.c_str());
                            }
                            return;
                        }
                        
                        // Clamp to 16-bit range
                        if (sample_value > 32767) sample_value = 32767;
                        if (sample_value < -32768) sample_value = -32768;
                        
                        channel_data[ch].push_back((int16_t)sample_value);
                    }
                }
                found_data = true;
            }
            else {
                // Skip unknown chunks
                if (chunk_size % 2 == 1) chunk_size++; // Align to word boundary
                file.seekg(chunk_size, std::ios::cur);
            }
        }

        if (!found_data || channel_data.empty() || channel_data[0].empty()) {
            status_message = "No audio data found in WAV file";
            if (is_mp3 && !temp_wav.empty()) {
                remove(temp_wav.c_str());
            }
            return;
        }

        sample_rate = sample_rate_val;
        channels = num_channels;
        size_t samples = channel_data[0].size();

        // Convert to mono for display and processing
        pcm_data.resize(samples);
        for(size_t i = 0; i < samples; ++i) {
            int32_t sum = 0;
            for(int c = 0; c < channels; ++c) {
                if (i < channel_data[c].size())
                    sum += channel_data[c][i];
            }
            pcm_data[i] = (short)(sum / channels);
        }

        start_point = 0;
        end_point = (int)samples;
        
        stop_preview(); // Stop any existing preview

        status_message = "Loaded " + std::string(filename);
        current_filename = filename;
        strncpy(input_path, filename, sizeof(input_path)-1);
        
        // Clean up temporary WAV file if we converted from MP3
        if (is_mp3 && !temp_wav.empty()) {
            remove(temp_wav.c_str());
        }
        
    } catch (std::exception& e) {
        status_message = "Error loading file: " + std::string(e.what());
        // Clean up temporary file on error
        if (is_mp3 && !temp_wav.empty()) {
            remove(temp_wav.c_str());
        }
    }
}

void PCM_Tool_Window::save_file(const char* filename)
{
    // Not used directly, implementing resample_and_save
}

void PCM_Tool_Window::start_preview()
{
    stop_preview();
    
    if (pcm_data.empty()) return;
    
    // Reset playback position
    current_playback_position = start_point;
    
    // Create new stream
    // We copy the current pcm_data state to the stream to be safe
    std::shared_ptr<PCM_Preview_Stream> stream = std::make_shared<PCM_Preview_Stream>(
        pcm_data, start_point, end_point, sample_rate, preview_loop, &current_playback_position
    );
    
    preview_stream = stream;
    Audio_Manager::get().add_stream(preview_stream);
}

void PCM_Tool_Window::stop_preview()
{
    if (preview_stream)
    {
        preview_stream->set_finished(true);
        preview_stream.reset();
    }
    current_playback_position = -1; // Reset position when stopped
}

void PCM_Tool_Window::resample_and_save(const char* filename)
{
    if (pcm_data.empty()) return;

    int target_rate = 17500;
    
    // We already have mono data in pcm_data
    // 1. Extract selection
    if (start_point < 0) start_point = 0;
    if (end_point > (int)pcm_data.size()) end_point = (int)pcm_data.size();
    if (start_point >= end_point) {
        status_message = "Invalid selection range";
        return;
    }

    std::vector<short> selection;
    selection.reserve(end_point - start_point);
    for(int i = start_point; i < end_point; ++i) {
        selection.push_back(pcm_data[i]);
    }

    // 2. Resample
    std::vector<short> resampled;
    if (sample_rate == target_rate) {
        resampled = selection;
    } else {
        double ratio = (double)sample_rate / (double)target_rate;
        int new_length = (int)(selection.size() / ratio);
        resampled.resize(new_length);
        
        for(int i = 0; i < new_length; ++i) {
            double src_idx = i * ratio;
            int idx0 = (int)src_idx;
            int idx1 = idx0 + 1;
            float frac = (float)(src_idx - idx0);
            
            if (idx1 >= (int)selection.size()) idx1 = idx0;
            
            float s0 = selection[idx0];
            float s1 = selection[idx1];
            resampled[i] = (short)(s0 + (s1 - s0) * frac);
        }
    }

    // 3. Apply speed doubling if enabled (skip every other sample)
    if (double_speed) {
        std::vector<short> speed_doubled;
        speed_doubled.reserve(resampled.size() / 2);
        for (size_t i = 0; i < resampled.size(); i += 2) {
            speed_doubled.push_back(resampled[i]);
        }
        resampled = speed_doubled;
    }

    // 4. Save
    std::ofstream out(filename, std::ios::binary);
    if (out) {
        // Write WAV Header
        uint32_t dataSize = (uint32_t)resampled.size() * sizeof(short);
        uint32_t fileSize = dataSize + 36;
        
        out.write("RIFF", 4);
        out.write((const char*)&fileSize, 4);
        out.write("WAVE", 4);
        out.write("fmt ", 4);
        
        uint32_t fmtSize = 16;
        uint16_t audioFormat = 1; // PCM
        uint16_t numChannels = 1;
        uint32_t sampleRate = 17500;
        uint32_t byteRate = sampleRate * numChannels * sizeof(short);
        uint16_t blockAlign = numChannels * sizeof(short);
        uint16_t bitsPerSample = 16;
        
        out.write((const char*)&fmtSize, 4);
        out.write((const char*)&audioFormat, 2);
        out.write((const char*)&numChannels, 2);
        out.write((const char*)&sampleRate, 4);
        out.write((const char*)&byteRate, 4);
        out.write((const char*)&blockAlign, 2);
        out.write((const char*)&bitsPerSample, 2);
        
        out.write("data", 4);
        out.write((const char*)&dataSize, 4);
        
        // Write Data
        out.write((char*)resampled.data(), dataSize);
        status_message = "Exported " + std::to_string(resampled.size()) + " samples to " + std::string(filename);
    } else {
        status_message = "Failed to write output file";
    }
}

void PCM_Tool_Window::resample_and_save_slices(const char* base_filename)
{
    if (pcm_data.empty()) return;
    if (num_slices < 1) {
        status_message = "Invalid number of slices";
        return;
    }

    int target_rate = 17500;
    
    // 1. Extract selection
    if (start_point < 0) start_point = 0;
    if (end_point > (int)pcm_data.size()) end_point = (int)pcm_data.size();
    if (start_point >= end_point) {
        status_message = "Invalid selection range";
        return;
    }

    std::vector<short> selection;
    selection.reserve(end_point - start_point);
    for(int i = start_point; i < end_point; ++i) {
        selection.push_back(pcm_data[i]);
    }

    // 2. Resample
    std::vector<short> resampled;
    if (sample_rate == target_rate) {
        resampled = selection;
    } else {
        double ratio = (double)sample_rate / (double)target_rate;
        int new_length = (int)(selection.size() / ratio);
        resampled.resize(new_length);
        
        for(int i = 0; i < new_length; ++i) {
            double src_idx = i * ratio;
            int idx0 = (int)src_idx;
            int idx1 = idx0 + 1;
            float frac = (float)(src_idx - idx0);
            
            if (idx1 >= (int)selection.size()) idx1 = idx0;
            
            float s0 = selection[idx0];
            float s1 = selection[idx1];
            resampled[i] = (short)(s0 + (s1 - s0) * frac);
        }
    }

    // 3. Apply speed doubling if enabled
    if (double_speed) {
        std::vector<short> speed_doubled;
        speed_doubled.reserve(resampled.size() / 2);
        for (size_t i = 0; i < resampled.size(); i += 2) {
            speed_doubled.push_back(resampled[i]);
        }
        resampled = speed_doubled;
    }

    // 4. Split into slices and save each
    std::string base_path = base_filename;
    // Remove .wav extension if present
    if (base_path.length() >= 4 && base_path.substr(base_path.length() - 4) == ".wav")
    {
        base_path = base_path.substr(0, base_path.length() - 4);
    }
    
    int samples_per_slice = (int)resampled.size() / num_slices;
    int saved_count = 0;
    
    for (int slice = 0; slice < num_slices; ++slice)
    {
        int slice_start = slice * samples_per_slice;
        int slice_end = (slice == num_slices - 1) ? (int)resampled.size() : (slice + 1) * samples_per_slice;
        
        std::vector<short> slice_data;
        slice_data.reserve(slice_end - slice_start);
        for (int i = slice_start; i < slice_end; ++i)
        {
            slice_data.push_back(resampled[i]);
        }
        
        // Generate filename: base-1.wav, base-2.wav, etc.
        std::string slice_filename = base_path + "-" + std::to_string(slice + 1) + ".wav";
        
        std::ofstream out(slice_filename, std::ios::binary);
        if (out) {
            // Write WAV Header
            uint32_t dataSize = (uint32_t)slice_data.size() * sizeof(short);
            uint32_t fileSize = dataSize + 36;
            
            out.write("RIFF", 4);
            out.write((const char*)&fileSize, 4);
            out.write("WAVE", 4);
            out.write("fmt ", 4);
            
            uint32_t fmtSize = 16;
            uint16_t audioFormat = 1; // PCM
            uint16_t numChannels = 1;
            uint32_t sampleRate = 17500;
            uint32_t byteRate = sampleRate * numChannels * sizeof(short);
            uint16_t blockAlign = numChannels * sizeof(short);
            uint16_t bitsPerSample = 16;
            
            out.write((const char*)&fmtSize, 4);
            out.write((const char*)&audioFormat, 2);
            out.write((const char*)&numChannels, 2);
            out.write((const char*)&sampleRate, 4);
            out.write((const char*)&byteRate, 4);
            out.write((const char*)&blockAlign, 2);
            out.write((const char*)&bitsPerSample, 2);
            
            out.write("data", 4);
            out.write((const char*)&dataSize, 4);
            
            // Write Data
            out.write((char*)slice_data.data(), dataSize);
            saved_count++;
        }
    }
    
    if (saved_count == num_slices) {
        status_message = "Exported " + std::to_string(num_slices) + " slices to " + base_path + "-*.wav";
    } else {
        status_message = "Exported " + std::to_string(saved_count) + " of " + std::to_string(num_slices) + " slices";
    }
}

void PCM_Tool_Window::load_pcm_data(const std::vector<short>& data, int rate, int ch, const std::string& name)
{
    pcm_data = data;
    sample_rate = rate;
    channels = ch;
    start_point = 0;
    end_point = (int)data.size();
    current_filename = name.empty() ? "Exported Selection" : name;
    status_message = "Loaded " + current_filename;
    stop_preview();
}

void PCM_Tool_Window::export_to_new_window()
{
    if (pcm_data.empty()) {
        status_message = "No data to export";
        return;
    }

    int target_rate = 17500;
    
    // 1. Extract selection
    if (start_point < 0) start_point = 0;
    if (end_point > (int)pcm_data.size()) end_point = (int)pcm_data.size();
    if (start_point >= end_point) {
        status_message = "Invalid selection range";
        return;
    }

    std::vector<short> selection;
    selection.reserve(end_point - start_point);
    for(int i = start_point; i < end_point; ++i) {
        selection.push_back(pcm_data[i]);
    }

    // 2. Resample
    std::vector<short> resampled;
    if (sample_rate == target_rate) {
        resampled = selection;
    } else {
        double ratio = (double)sample_rate / (double)target_rate;
        int new_length = (int)(selection.size() / ratio);
        resampled.resize(new_length);
        
        for(int i = 0; i < new_length; ++i) {
            double src_idx = i * ratio;
            int idx0 = (int)src_idx;
            int idx1 = idx0 + 1;
            float frac = (float)(src_idx - idx0);
            
            if (idx1 >= (int)selection.size()) idx1 = idx0;
            
            float s0 = selection[idx0];
            float s1 = selection[idx1];
            resampled[i] = (short)(s0 + (s1 - s0) * frac);
        }
    }

    // 3. Apply speed doubling if enabled
    if (double_speed) {
        std::vector<short> speed_doubled;
        speed_doubled.reserve(resampled.size() / 2);
        for (size_t i = 0; i < resampled.size(); i += 2) {
            speed_doubled.push_back(resampled[i]);
        }
        resampled = speed_doubled;
    }

    // 4. Create new window with processed data
    std::string export_name = current_filename.empty() ? "Exported Selection" : current_filename + " (exported)";
    main_window.create_pcm_tool_window_with_data(resampled, target_rate, 1, export_name);
        status_message = "Exported " + std::to_string(resampled.size()) + " samples to new window";
}

void PCM_Tool_Window::close_request()
{
    // If there's loaded data, show confirmation dialog
    if (!pcm_data.empty())
    {
        close_req_state = Window::CLOSE_IN_PROGRESS;
    }
    else
    {
        close_req_state = Window::CLOSE_OK;
    }
}

void PCM_Tool_Window::show_close_warning()
{
    modal_open = 1;
    std::string modal_id = "Close PCM Editor###PCMClose" + std::to_string(id);
    if (!ImGui::IsPopupOpen(modal_id.c_str()))
        ImGui::OpenPopup(modal_id.c_str());
    if(ImGui::BeginPopupModal(modal_id.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Close PCM Editor?\nAll content will be lost.\n");
        ImGui::Separator();

        if (ImGui::Button("OK", ImVec2(120, 0)))
        {
            close_req_state = Window::CLOSE_OK;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
        {
            close_req_state = Window::NO_CLOSE_REQUEST;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void PCM_Tool_Window::cleanup()
{
    // Stop preview playback
    stop_preview();
    
    // Clear all PCM data to free RAM
    pcm_data.clear();
    pcm_data.shrink_to_fit(); // Release memory
    
    // Reset state
    sample_rate = 0;
    channels = 0;
    start_point = 0;
    end_point = 0;
    current_filename.clear();
    status_message = "Ready";
}
