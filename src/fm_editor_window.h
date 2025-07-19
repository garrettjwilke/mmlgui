// fm_editor_window.h
#ifndef FM_EDITOR_WINDOW_H
#define FM_EDITOR_WINDOW_H

#include <string>
#include <vector>
#include <array>
#include <regex>
#include <sstream>
#include <iomanip>
#include "imgui.h"

class TextEditor; // Forward declaration

class FM_Editor_Window {
public:
    FM_Editor_Window();
    void display(TextEditor& editor);
    void parse_fm_params_from_mml(const std::string& mml, int instrument_num);
    void write_fm_params_to_mml(TextEditor& editor, int instrument_num);

    int get_instrument_num() const;
    void set_instrument_num(int num);

private:
    int instrument_num;
    int fm_alg;
    int fm_feedback;
    std::array<std::array<int, 10>, 4> fm_params;
    std::string fm_instrument_name;
};

#endif // FM_EDITOR_WINDOW_H
