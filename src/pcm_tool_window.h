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

private:
    void load_file(const char* filename);
    void save_file(const char* filename);
    void resample_and_save(const char* filename);
    void start_preview();
    void stop_preview();

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

    std::string status_message;
    std::string current_filename;
    std::string pending_save_path;
    
    // Waveform visualization helper
    static float WaveformGetter(void* data, int idx);
};

#endif //PCM_TOOL_WINDOW_H

