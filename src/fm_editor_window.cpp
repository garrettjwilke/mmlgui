// fm_editor_window.cpp
#include "fm_editor_window.h"
#include "TextEditor.h" // Include the TextEditor header

FM_Editor_Window::FM_Editor_Window()
    : instrument_num(1), fm_alg(0), fm_feedback(0), fm_params{} {}

void FM_Editor_Window::display(TextEditor& editor) {
    const int param_min[10] = {  0,  0,  0,  0,  0,   0,  0,  0,  0,   0 };
    const int param_max[10] = { 31, 31, 31, 15, 15, 127, 3, 15, 7,  15 };
    ImGui::SetNextWindowPos(ImVec2(800, 100), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("FM Instrument Editor", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        bool changed = false;
        static int prev_instrument_num = -1;
        ImGui::InputInt("Instrument #", &instrument_num);
        if (instrument_num < 1) instrument_num = 1;
        if (instrument_num != prev_instrument_num) {
            prev_instrument_num = instrument_num;
            parse_fm_params_from_mml(editor.GetText(), instrument_num);
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        changed |= ImGui::InputInt("Algorithm", &fm_alg);
        changed |= ImGui::InputInt("Feedback", &fm_feedback);
        if (fm_alg < 0) fm_alg = 0;
        if (fm_alg > 7) fm_alg = 7;
        if (fm_feedback < 0) fm_feedback = 0;
        if (fm_feedback > 7) fm_feedback = 7;

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        const char* labels[10] = {
            "AR", "DR", "SR", "RR", "SL", "TL", "KS", "ML", "DT", "SSG"
        };

        for (int op = 0; op < 4; ++op) {
            if (op > 0 && op % 2 == 0)
                ImGui::NewLine();

            ImGui::BeginGroup();
            ImGui::Text("        - Operator %d -", op + 1);

            for (int i = 0; i < 10; ++i) {
                ImGui::PushID(op * 10 + i);
                if (labels[i] == "SSG") {
                    ImGui::Text("%s", labels[i]);
                } else {
                    ImGui::Text("%s ", labels[i]);
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(150);
                //changed |= ImGui::SliderInt("", &fm_params[op][i], 0, 127);
                changed |= ImGui::SliderInt("", &fm_params[op][i], param_min[i], param_max[i]);
                ImGui::PopID();
            }
            ImGui::EndGroup();
            if (op % 2 == 0)
                ImGui::SameLine();
        }

        if (changed) {
            write_fm_params_to_mml(editor, instrument_num);
        }
    }
    ImGui::End();
}

void FM_Editor_Window::parse_fm_params_from_mml(const std::string& mml, int instrument_num) {
    std::regex pattern(
        "@" + std::to_string(instrument_num) + R"( +fm +\d+ +\d+[^\@]*)",
        std::regex_constants::icase
    );

    std::smatch match;
    if (std::regex_search(mml, match, pattern)) {
        std::istringstream stream(match[0]);
        std::string line;
        std::string header;
        std::getline(stream, header);
        std::istringstream headerStream(header);

        std::string atToken, fmToken;
        headerStream >> atToken >> fmToken >> fm_alg >> fm_feedback;

        for (int op = 0; op < 4; ++op) {
            if (!std::getline(stream, line)) break;

            std::istringstream linestream(line);
            std::string value;
            int param_index = 0;

            while (std::getline(linestream, value, ',') && param_index < 10) {
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);

                try {
                    fm_params[op][param_index] = std::stoi(value);
                } catch (...) {
                    fm_params[op][param_index] = 0;
                }

                ++param_index;
            }

            while (param_index < 10) {
                fm_params[op][param_index++] = 0;
            }
        }
    } else {
        for (int op = 0; op < 4; ++op)
            for (int p = 0; p < 10; ++p)
                fm_params[op][p] = 0;
    }
}

void FM_Editor_Window::write_fm_params_to_mml(TextEditor& editor, int instrument_num) {
    std::ostringstream instrument_string;
    instrument_string << "@" << instrument_num << " fm " << fm_alg << " " << fm_feedback << " ; \n";
    for (int op = 0; op < 4; ++op) {
        for (int p = 0; p < 10; ++p) {
            if (p == 0) {
                instrument_string << " " << std::setw(3) << std::right << fm_params[op][p];
            } else {
                instrument_string << std::setw(3) << std::right << fm_params[op][p];
            }
            if (p < 9)
                instrument_string << ",";
        }
        instrument_string << "\n";
    }
    instrument_string << "\n";

    std::string new_block = instrument_string.str();
    std::string mml = editor.GetText();

    std::regex pattern("@" + std::to_string(instrument_num) + "\\s+fm[^@]*", std::regex_constants::icase);
    std::smatch match;
    if (std::regex_search(mml, match, pattern)) {
        mml.replace(match.position(0), match.length(0), new_block);
    } else {
        mml = new_block + "\n" + mml;
    }

    editor.SetText(mml);
}

int FM_Editor_Window::get_instrument_num() const {
    return instrument_num;
}

void FM_Editor_Window::set_instrument_num(int num) {
    instrument_num = num;
}
