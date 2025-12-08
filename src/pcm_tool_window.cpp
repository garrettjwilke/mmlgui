#include "pcm_tool_window.h"
#include "imgui.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cmath>
#include "wave.h"
#include "stringf.h"
#include "audio_manager.h"

// Simple Audio Stream for Preview
class PCM_Preview_Stream : public Audio_Stream
{
public:
    PCM_Preview_Stream(const std::vector<short>& data, int start, int end, int rate, bool loop)
        : data(data), start(start), end(end), rate(rate), loop(loop), pos(0.0), step(0.0)
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

            output[i].L = val;
            output[i].R = val; 

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
};

PCM_Tool_Window::PCM_Tool_Window() : Window(), fs(true, false, true), browse_open(false), browse_save(false)
{
    type = WT_PCM_TOOL;
    sample_rate = 0;
    channels = 0;
    start_point = 0;
    end_point = 0;
    preview_loop = false;
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
    if (!active) return;

    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("PCM Tool", &active))
    {
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
            const char* path = fs.chooseFileDialog(load_clicked, input_path, ".wav", "Load WAV", size, pos);
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

            // Waveform display
            ImVec2 content_region = ImGui::GetContentRegionAvail();
            float plot_height = 150.0f;
            content_region.y = plot_height;
            
            ImGui::PlotLines("Waveform", WaveformGetter, (void*)&pcm_data, (int)pcm_data.size(), 0, NULL, -1.0f, 1.0f, ImVec2(content_region.x, plot_height));
            
            // Get drawing context
            ImGuiStyle& style = ImGui::GetStyle();
            ImVec2 plot_min = ImGui::GetItemRectMin();
            ImVec2 plot_max = ImGui::GetItemRectMax();
            
            // Adjust to inner graph area (excluding padding)
            plot_min.x += style.FramePadding.x;
            plot_max.x -= style.FramePadding.x;
            plot_min.y += style.FramePadding.y;
            plot_max.y -= style.FramePadding.y;

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            
            // Interaction logic for drag tabs
            if (pcm_data.size() > 0)
            {
                // Use (size - 1) to match PlotLines behavior which draws samples 0..N-1 across the width
                float width = plot_max.x - plot_min.x;
                float count = (float)(pcm_data.size() > 1 ? pcm_data.size() - 1 : 1);
                float x_step = width / count;
                float handle_size = 10.0f;
                
                // Helper to draw and handle tab interaction
                auto handle_tab = [&](int* point, bool is_top, ImU32 color, const char* id) {
                    if (*point < 0) *point = 0;
                    if (*point > (int)pcm_data.size()) *point = (int)pcm_data.size();
                    
                    float x = plot_min.x + (*point) * x_step;
                    
                    // Clamp visual position to plot bounds
                    if (x > plot_max.x) x = plot_max.x;
                    if (x < plot_min.x) x = plot_min.x;

                    ImVec2 tab_pos = is_top ? ImVec2(x, plot_min.y) : ImVec2(x, plot_max.y);
                    
                    // Triangle tab shape
                    ImVec2 p1 = tab_pos;
                    ImVec2 p2 = ImVec2(tab_pos.x - handle_size/2, tab_pos.y + (is_top ? -handle_size : handle_size));
                    ImVec2 p3 = ImVec2(tab_pos.x + handle_size/2, tab_pos.y + (is_top ? -handle_size : handle_size));
                    
                    // Invisible button for interaction
                    ImGui::SetCursorScreenPos(ImVec2(p2.x, is_top ? p2.y : p1.y));
                    ImGui::InvisibleButton(id, ImVec2(handle_size, handle_size));
                    
                    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0))
                    {
                        float delta_x = ImGui::GetIO().MouseDelta.x;
                        *point += (int)(delta_x / x_step);
                        // Clamp again
                        if (*point < 0) *point = 0;
                        if (*point > (int)pcm_data.size()) *point = (int)pcm_data.size();
                    }
                    
                    // Draw tab
                    draw_list->AddTriangleFilled(p1, p2, p3, color);
                    // Draw line
                    draw_list->AddLine(ImVec2(x, plot_min.y), ImVec2(x, plot_max.y), color, 2.0f);
                };

                // Start Point (Green) - Top Tab
                handle_tab(&start_point, true, IM_COL32(0, 255, 0, 255), "##start_tab");
                
                // End Point (Red) - Bottom Tab
                handle_tab(&end_point, false, IM_COL32(255, 0, 0, 255), "##end_tab");
            }

            // Sliders for Start/End
            int max_sample = (int)pcm_data.size();
            if (end_point > max_sample) end_point = max_sample;
            if (start_point >= end_point) start_point = end_point - 1;

            ImGui::DragInt("Start Point", &start_point, 1.0f, 0, end_point - 1);
            ImGui::DragInt("End Point", &end_point, 1.0f, start_point + 1, max_sample);
            
            ImGui::Checkbox("Loop Preview", &preview_loop);
            ImGui::SameLine();
            
            bool is_playing = (preview_stream && !preview_stream->get_finished());
            if (ImGui::Button(is_playing ? "Stop Preview" : "Preview"))
            {
                if (is_playing)
                    stop_preview();
                else
                    start_preview();
            }

            ImGui::Separator();
            bool save_clicked = ImGui::Button("Export (17.5kHz Mono s16le)...");
            if (save_clicked)
            {
                browse_save = true;
                browse_open = false;
            }

             if (browse_save)
            {
                // Center the dialog
                ImVec2 center = ImGui::GetIO().DisplaySize;
                center.x *= 0.5f;
                center.y *= 0.5f;
                ImVec2 size(600, 400);
                ImVec2 pos = ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f);

                const char* path = fs.saveFileDialog(save_clicked, input_path, "output.wav", ".wav", "Save PCM", size, pos);
                if (strlen(path) > 0)
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
                    resample_and_save(pending_save_path.c_str());
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
    try {
        // Use the default constructor and then read
        Wave_File wav(0,0,0);
        if (wav.read(filename) != 0) {
             status_message = "Failed to read WAV file";
             return;
        }

        sample_rate = wav.get_rate();
        channels = wav.get_channels();
        const auto& data = wav.get_data();

        if (data.empty()) {
            status_message = "No data in WAV file";
            return;
        }

        // Convert to mono for display and processing
        // data is [channel][sample]
        size_t samples = data[0].size();
        pcm_data.resize(samples);
        
        for(size_t i = 0; i < samples; ++i) {
            int32_t sum = 0;
            for(int c = 0; c < channels; ++c) {
                if (i < data[c].size())
                    sum += data[c][i];
            }
            pcm_data[i] = (short)(sum / channels);
        }

        start_point = 0;
        end_point = (int)samples;
        
        stop_preview(); // Stop any existing preview

        status_message = "Loaded " + std::string(filename);
        current_filename = filename;
        strncpy(input_path, filename, sizeof(input_path)-1);
        
    } catch (std::exception& e) {
        status_message = "Error loading file: " + std::string(e.what());
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
    
    // Create new stream
    // We copy the current pcm_data state to the stream to be safe
    std::shared_ptr<PCM_Preview_Stream> stream = std::make_shared<PCM_Preview_Stream>(
        pcm_data, start_point, end_point, sample_rate, preview_loop
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

    // 3. Save
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
