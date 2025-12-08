#ifndef PCM_TOOL_WINDOW_H
#define PCM_TOOL_WINDOW_H

#include "window.h"
#include "addons/imguifilesystem/imguifilesystem.h"
#include "audio_manager.h"
#include <vector>
#include <string>
#include <memory>

class PCM_Tool_Window : public Window
{
public:
    PCM_Tool_Window();
    void display() override;
    void close_request() override;
    void load_pcm_data(const std::vector<short>& data, int rate, int ch, const std::string& name = "");

private:
    void load_file(const char* filename);
    void save_file(const char* filename);
    void resample_and_save(const char* filename);
    void resample_and_save_slices(const char* base_filename);
    void export_to_new_window();
    void start_preview();
    void stop_preview();
    void show_close_warning();
    void cleanup();

    ImGuiFs::Dialog fs;
    bool browse_open;
    bool browse_save;
    char input_path[1024];

    std::vector<short> pcm_data;
    int sample_rate;
    int channels;
    int start_point;
    int end_point;
    
    bool preview_loop;
    bool double_speed;
    std::shared_ptr<Audio_Stream> preview_stream;
    int current_playback_position; // Current playback position in samples
    
    // Slicing options
    bool slice_enabled;
    int num_slices;
    
    // Zoom state
    bool zoom_enabled;
    int zoom_point; // 0 = start point, 1 = end point
    float zoom_level; // Zoom multiplier (1.0 = no zoom, higher = more zoom)
    int zoom_window_samples; // Number of samples visible in zoom view

    std::string status_message;
    std::string current_filename;
    std::string pending_save_path;
    
    // Waveform visualization helper
    static float WaveformGetter(void* data, int idx);
};

#endif //PCM_TOOL_WINDOW_H

