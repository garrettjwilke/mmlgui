// fm_editor_window.cpp
#include "imgui.h"
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
    std::istringstream stream(mml);
    std::string line;
    bool in_block = false;
    std::vector<std::string> block_lines;

    const std::string start_marker = "@" + std::to_string(instrument_num) + " fm";
    const std::string end_marker = ";endFM";

    while (std::getline(stream, line)) {
        std::string trimmed = line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));
        trimmed.erase(trimmed.find_last_not_of(" \t") + 1);

        if (!in_block && trimmed.rfind(start_marker, 0) == 0) {
            in_block = true;
            block_lines.push_back(line);
        } else if (in_block) {
            block_lines.push_back(line);
            if (trimmed == end_marker) break;
        }
    }

    if (block_lines.size() >= 6) { // 1 header + 4 ops + end marker
        std::istringstream headerStream(block_lines[0]);
        std::string atToken, fmToken;
        headerStream >> atToken >> fmToken >> fm_alg >> fm_feedback;

        for (int op = 0; op < 4; ++op) {
            std::istringstream linestream(block_lines[1 + op]);
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
        // invalid or incomplete block, reset
        for (int op = 0; op < 4; ++op)
            for (int p = 0; p < 10; ++p)
                fm_params[op][p] = 0;
    }
}


void FM_Editor_Window::write_fm_params_to_mml(TextEditor& editor, int instrument_num) {
    std::ostringstream instrument_string;
    instrument_string << "@" << instrument_num << " fm " << fm_alg << " " << fm_feedback << " ;\n";
    for (int op = 0; op < 4; ++op) {
        for (int p = 0; p < 10; ++p) {
            instrument_string << (p == 0 ? " " : "") << std::setw(3) << std::right << fm_params[op][p];
            if (p < 9) instrument_string << ",";
        }
        instrument_string << "\n";
    }
    instrument_string << "; -- end FM\n";

    std::string mml = editor.GetText();
    std::istringstream mml_stream(mml);
    std::ostringstream new_mml;
    std::string line;
    bool in_block = false;
    bool replaced = false;
    bool inserted = false;
    std::string start_marker = "@" + std::to_string(instrument_num) + " fm";
    std::string end_marker = "; -- end FM";

    // Track all existing FM instrument headers with their line positions
    std::vector<std::pair<int, int>> instrument_blocks; // {instrument_num, insert_pos}
    std::ostringstream buffer;
    int line_num = 0;
    int last_pos = 0;
    int insert_after_pos = -1;

    std::vector<std::string> lines;
    while (std::getline(mml_stream, line)) {
        lines.push_back(line);
    }

    // Identify all @N fm ... blocks
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string trimmed = lines[i];
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));
        trimmed.erase(trimmed.find_last_not_of(" \t") + 1);

        std::smatch match;
        if (std::regex_match(trimmed, match, std::regex(R"(@(\d+)\s+fm.*)", std::regex::icase))) {
            int found_num = std::stoi(trimmed.substr(1));
            size_t end_idx = i + 1;
            while (end_idx < lines.size()) {
                std::string end_trimmed = lines[end_idx];
                end_trimmed.erase(0, end_trimmed.find_first_not_of(" \t"));
                end_trimmed.erase(end_trimmed.find_last_not_of(" \t") + 1);
                if (end_trimmed == end_marker) break;
                ++end_idx;
            }
            instrument_blocks.emplace_back(found_num, end_idx);
        }
    }

    // Check if we're replacing an existing block
    for (const auto& [num, pos] : instrument_blocks) {
        if (num == instrument_num) {
            // Replace the block from @N to ; -- end FM
            size_t start_idx = 0;
            for (; start_idx < lines.size(); ++start_idx) {
                std::string trimmed = lines[start_idx];
                trimmed.erase(0, trimmed.find_first_not_of(" \t"));
                trimmed.erase(trimmed.find_last_not_of(" \t") + 1);
                if (trimmed.rfind("@" + std::to_string(instrument_num) + " fm", 0) == 0) break;
            }

            size_t end_idx = start_idx;
            while (end_idx < lines.size()) {
                std::string trimmed = lines[end_idx];
                trimmed.erase(0, trimmed.find_first_not_of(" \t"));
                trimmed.erase(trimmed.find_last_not_of(" \t") + 1);
                if (trimmed == end_marker) break;
                ++end_idx;
            }

            // Write the full replacement block
            for (size_t i = 0; i < start_idx; ++i)
                new_mml << lines[i] << "\n";
            new_mml << instrument_string.str();
            for (size_t i = end_idx + 1; i < lines.size(); ++i)
                new_mml << lines[i] << "\n";
            replaced = true;
            break;
        }
    }

    // Insert a new block if not replaced
    if (!replaced) {
        // Find the next-lowest instrument to insert after
        int best_match_num = -1;
        int best_match_pos = -1;
        for (const auto& [num, pos] : instrument_blocks) {
            if (num < instrument_num && num > best_match_num) {
                best_match_num = num;
                best_match_pos = pos;
            }
        }

        for (size_t i = 0; i < lines.size(); ++i) {
            new_mml << lines[i] << "\n";
            if (!inserted && static_cast<int>(i) == best_match_pos) {
                new_mml << "\n" << instrument_string.str();
                inserted = true;
            }
        }

        if (!inserted) {
            // No lower instrument found, just append
            new_mml << "\n" << instrument_string.str();
        }
    }

    editor.SetText(new_mml.str());
}



int FM_Editor_Window::get_instrument_num() const {
    return instrument_num;
}

void FM_Editor_Window::set_instrument_num(int num) {
    instrument_num = num;
}
