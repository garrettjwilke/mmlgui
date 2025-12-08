#ifndef PCM_TOOL_WINDOW_H
#define PCM_TOOL_WINDOW_H

#include "window.h"
#include "addons/imguifilesystem/imguifilesystem.h"
#include <vector>
#include <string>

class PCM_Tool_Window : public Window
{
public:
    PCM_Tool_Window();
    void display() override;

private:
    void load_file(const char* filename);
    void save_file(const char* filename);
    void resample_and_save(const char* filename);

    ImGuiFs::Dialog fs;
    bool browse_open;
    bool browse_save;
    char input_path[1024];

    std::vector<short> pcm_data;
    int sample_rate;
    int channels;
    int start_point;
    int end_point;
    
    std::string status_message;
    std::string current_filename;
    
    // Waveform visualization helper
    static float WaveformGetter(void* data, int idx);
};

#endif //PCM_TOOL_WINDOW_H

