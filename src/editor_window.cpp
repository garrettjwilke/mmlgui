/*
	MML text editor

	todo
		1. file i/o key shortcuts
		2. syntax highlighting for MML

	bugs in the editor (to be fixed in my fork of ImGuiColorTextEdit)
		1. highlighted area when doubleclicking doesn't take account punctuation
			(based on the code, This should hopefully automatically be fixed when
			 MML highlighting is done)
		2. data copied to clipboard from mmlgui has UNIX-linebreaks regardless of OS.
		   This may break some windows tools (such as Notepad.exe)

	"bugs" in the imgui addons branch (maybe can be fixed locally and upstreamed?)
		1. file dialog doesn't automatically focus the filename.
		2. file dialog has no keyboard controls at all :/
*/

#include "main_window.h"
#include "editor_window.h"
#include "track_view_window.h"
#include "track_list_window.h"

#include "dmf_importer.h"

#include "imgui.h"

#include "core.h"
#include "song.h"
#include "track.h"
#include "player.h"

#include <GLFW/glfw3.h>
#include <string>
#include <cstring>
#include <fstream>

enum Flags
{
	MODIFIED		= 1<<0,
	FILENAME_SET	= 1<<1,
	NEW				= 1<<2,
	OPEN			= 1<<3,
	SAVE			= 1<<4,
	SAVE_AS			= 1<<5,
	DIALOG			= 1<<6,
	IGNORE_WARNING	= 1<<7,
	RECOMPILE       = 1<<8,
	EXPORT			= 1<<9,
	IMPORT			= 1<<10,
};

Editor_Window::Editor_Window()
	: editor()
	, filename(default_filename)
	, flag(RECOMPILE)
	, line_pos(0)
	, cursor_pos(0)
{
	type = WT_EDITOR;
	editor.SetColorizerEnable(false); // disable syntax highlighting for now
	song_manager = std::make_shared<Song_Manager>();
	// Set initial palette based on current theme
	set_editor_palette(main_window.is_light_theme());
}

void Editor_Window::display()
{
	bool keep_open = true;

	if(test_flag(RECOMPILE))
	{
		if(!song_manager->get_compile_in_progress())
		{
			if(!song_manager->compile(editor.GetText(), filename))
				clear_flag(RECOMPILE);
		}
	}

	std::string window_id;
	window_id = get_display_filename();
	if(test_flag(MODIFIED))
		window_id += "*";
	window_id += "###Editor" + std::to_string(id);

	auto cpos = editor.GetCursorPosition();
	ImGui::Begin(window_id.c_str(), &keep_open, /*ImGuiWindowFlags_HorizontalScrollbar |*/ ImGuiWindowFlags_MenuBar);
	ImGui::SetWindowSize(ImVec2(1050, 900), ImGuiCond_Once);
	ImGui::SetWindowPos(ImVec2(10, 10), ImGuiCond_Once);

	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New", "Ctrl+N"))
				set_flag(NEW);
			if (ImGui::MenuItem("Open...", "Ctrl+O"))
				set_flag(OPEN|DIALOG);
			if (ImGui::MenuItem("Save", "Ctrl+S", nullptr, test_flag(FILENAME_SET)))
				set_flag(SAVE|DIALOG);
			if (ImGui::MenuItem("Save As...", "Ctrl+Alt+S"))
				set_flag(SAVE|SAVE_AS|DIALOG);
			ImGui::Separator();
			if (ImGui::MenuItem("Import patches from DMF...", nullptr, nullptr, !editor.IsReadOnly()))
				set_flag(IMPORT|DIALOG);
			show_export_menu();
			if (ImGui::MenuItem("mdslink export...", nullptr, nullptr))
			{
				main_window.show_export_window();
			}
			if (ImGui::MenuItem("PCM tool...", nullptr, nullptr))
			{
				// Get current editor window position and offset for PCM tool
				ImVec2 offset_pos = ImVec2(350, 50);
				main_window.show_pcm_tool_window(&offset_pos);
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Close", "Ctrl+W"))
				keep_open = false;
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Player"))
		{
			if (ImGui::MenuItem("Play", "F5"))
				play_song();
			if (ImGui::MenuItem("Play from start of line", "F6"))
				play_from_line();
			if (ImGui::MenuItem("Play from cursor", "F7"))
				play_from_cursor();
			if (ImGui::MenuItem("Stop", "Escape or F8"))
				stop_song();
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Edit"))
		{
			bool ro = editor.IsReadOnly();

			if (ImGui::MenuItem("Undo", "Ctrl+Z or Alt+Backspace", nullptr, !ro && editor.CanUndo()))
				editor.Undo(), set_flag(MODIFIED|RECOMPILE);
			if (ImGui::MenuItem("Redo", "Ctrl+Y", nullptr, !ro && editor.CanRedo()))
				editor.Redo(), set_flag(MODIFIED|RECOMPILE);

			ImGui::Separator();

			if (ImGui::MenuItem("Cut", "Ctrl+X", nullptr, !ro && editor.HasSelection()))
				editor.Cut(), set_flag(MODIFIED|RECOMPILE);
			if (ImGui::MenuItem("Copy", "Ctrl+C", nullptr, editor.HasSelection()))
				editor.Copy();
			if (ImGui::MenuItem("Delete", "Del", nullptr, !ro && editor.HasSelection()))
				editor.Delete(), set_flag(MODIFIED|RECOMPILE);

			GLFWerrorfun prev_error_callback = glfwSetErrorCallback(NULL); // disable clipboard error messages...

			if (ImGui::MenuItem("Paste", "Ctrl+V", nullptr, !ro && ImGui::GetClipboardText() != nullptr))
				editor.Paste(), set_flag(MODIFIED|RECOMPILE);

			glfwSetErrorCallback(prev_error_callback);

			ImGui::Separator();

			if (ImGui::MenuItem("Select All", "Ctrl+A", nullptr))
				editor.SetSelection(TextEditor::Coordinates(), TextEditor::Coordinates(editor.GetTotalLines(), 0));

			ImGui::Separator();

			if (ImGui::MenuItem("Read-only mode", nullptr, &ro))
				editor.SetReadOnly(ro);

#ifdef DEBUG
			ImGui::Separator();

			if (ImGui::MenuItem("Configuration..."))
				main_window.show_config_window();
#endif

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("View"))
		{
			if (ImGui::MenuItem("Track view..."))
			{
				children.push_back(std::make_shared<Track_View_Window>(song_manager));
			}
			if (ImGui::MenuItem("Track list..."))
			{
				children.push_back(std::make_shared<Track_List_Window>(song_manager));
			}
			ImGui::Separator();
			if (ImGui::BeginMenu("Editor style"))
			{
				if (ImGui::MenuItem("Dark palette"))
					set_palette_with_theme_text(TextEditor::GetDarkPalette(), false);
				if (ImGui::MenuItem("Light palette"))
					set_palette_with_theme_text(TextEditor::GetLightPalette(), true);
				if (ImGui::MenuItem("Retro blue palette"))
					set_palette_with_theme_text(TextEditor::GetRetroBluePalette(), false);
				ImGui::EndMenu();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("UI Settings..."))
			{
				main_window.show_ui_settings_window();
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Help"))
		{
			if (ImGui::MenuItem("About..."))
			{
				main_window.show_about_window();
			}
			ImGui::EndMenu();
		}

		ImGui::EndMenuBar();
	}

	// focus on the text editor rather than the "frame"
	if (ImGui::IsWindowFocused())
		ImGui::SetNextWindowFocus();

	show_track_positions();

	GLFWerrorfun prev_error_callback = glfwSetErrorCallback(NULL); // disable clipboard error messages...

	editor.Render("EditorArea", ImVec2(0, -ImGui::GetFrameHeight()));

	glfwSetErrorCallback(prev_error_callback);

	if(editor.IsTextChanged())
		set_flag(MODIFIED|RECOMPILE);

	if (ImGui::IsWindowFocused() | editor.IsWindowFocused())
	{
		ImGuiIO& io = ImGui::GetIO();
		auto isOSX = io.ConfigMacOSXBehaviors;
		auto alt = io.KeyAlt;
		auto ctrl = io.KeyCtrl;
		auto shift = io.KeyShift;
		auto super = io.KeySuper;
		auto isShortcut = (isOSX ? (super && !ctrl) : (ctrl && !super)) && !alt && !shift;
		auto isAltShortcut = (isOSX ? (super && !ctrl) : (ctrl && !super)) && alt && !shift;

		if (isShortcut && ImGui::IsKeyPressed(GLFW_KEY_N))
			set_flag(NEW);
		else if (isShortcut && ImGui::IsKeyPressed(GLFW_KEY_O))
			set_flag(OPEN|DIALOG);
		else if (isShortcut && ImGui::IsKeyPressed(GLFW_KEY_S))
			set_flag(SAVE|DIALOG);
		else if (isAltShortcut && ImGui::IsKeyPressed(GLFW_KEY_S))
			set_flag(SAVE|SAVE_AS|DIALOG);
		else if (isShortcut && ImGui::IsKeyPressed(GLFW_KEY_W))
			keep_open = false;
		else if(ImGui::IsKeyPressed(GLFW_KEY_ESCAPE) || ImGui::IsKeyPressed(GLFW_KEY_F8))
			stop_song();
		else if(ImGui::IsKeyPressed(GLFW_KEY_F5))
			play_song();
		else if(ImGui::IsKeyPressed(GLFW_KEY_F6))
			play_from_line();
		else if(ImGui::IsKeyPressed(GLFW_KEY_F7))
			play_from_cursor();

		song_manager->set_editor_position({cpos.mLine, cpos.mColumn});
		line_pos = song_manager->get_song_pos_at_line();
		cursor_pos = song_manager->get_song_pos_at_cursor();
	}
	else
	{
		song_manager->set_editor_position({-1, -1});
	}

	//ImGui::Spacing();
	ImGui::AlignTextToFramePadding();
	ImGui::Text("%6d:%-6d %6d line%c | %s ", cpos.mLine + 1, cpos.mColumn + 1, editor.GetTotalLines(), (editor.GetTotalLines() == 1) ? ' ' : 's',
		editor.IsOverwrite() ? "Ovr" : "Ins");

	ImGui::SameLine();
	ImGui::Text("| L:%5d C:%5d", line_pos, cursor_pos);

	//get_compile_result();
	show_player_controls();

	ImGui::End();

	if(!keep_open)
		close_request_all();

	if(player_error.size() && !modal_open)
		show_player_error();

	if(get_close_request() == Window::CLOSE_IN_PROGRESS && !modal_open)
		show_close_warning();

	if(get_close_request() == Window::CLOSE_OK)
	{
		active = false;
		song_manager->stop();
	}
	else
	{
		handle_file_io();
	}
}

void Editor_Window::close_request()
{
	if(test_flag(MODIFIED))
		close_req_state = Window::CLOSE_IN_PROGRESS;
	else
		close_req_state = Window::CLOSE_OK;
}

void Editor_Window::play_song(uint32_t position)
{
	try
	{
		song_manager->play(position);
	}
	catch(InputError& except)
	{
		player_error = except.what();
	}
	catch(std::exception& except)
	{
		player_error = "exception type: ";
		player_error += typeid(except).name();
		player_error += "\nexception message: ";
		player_error += except.what();
	}
}

void Editor_Window::stop_song()
{
	song_manager->stop();
}

void Editor_Window::play_from_cursor()
{
	play_song(cursor_pos);
}

void Editor_Window::play_from_line()
{
	play_song(line_pos);
}

void Editor_Window::show_player_error()
{
	modal_open = 1;
	std::string modal_id;
	modal_id = get_display_filename() + "###modal";
	if (!ImGui::IsPopupOpen(modal_id.c_str()))
		ImGui::OpenPopup(modal_id.c_str());
	if(ImGui::BeginPopupModal(modal_id.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("%s\n", player_error.c_str());
		ImGui::Separator();

		ImGui::SetItemDefaultFocus();
		if (ImGui::Button("OK", ImVec2(120, 0)))
		{
			player_error = "";
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void Editor_Window::show_close_warning()
{
	modal_open = 1;
	std::string modal_id;
	modal_id = get_display_filename() + "###modal";
	if (!ImGui::IsPopupOpen(modal_id.c_str()))
		ImGui::OpenPopup(modal_id.c_str());
	if(ImGui::BeginPopupModal(modal_id.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("You have unsaved changes. Close anyway?\n");
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
			close_req_state = Window::CLOSE_NOT_OK;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void Editor_Window::show_warning()
{
	modal_open = 1;
	std::string modal_id;
	modal_id = get_display_filename() + "###modal";
	if (!ImGui::IsPopupOpen(modal_id.c_str()))
		ImGui::OpenPopup(modal_id.c_str());
	if(ImGui::BeginPopupModal(modal_id.c_str(), NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("You have unsaved changes. Continue anyway?\n");
		ImGui::Separator();

		if (ImGui::Button("OK", ImVec2(120, 0)))
		{
			set_flag(IGNORE_WARNING);
			ImGui::CloseCurrentPopup();
		}
		ImGui::SetItemDefaultFocus();
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120, 0)))
		{
			clear_flag(NEW|OPEN);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void Editor_Window::handle_file_io()
{
	// new file requested
	if(test_flag(NEW) && !modal_open)
	{
		if(test_flag(MODIFIED) && !test_flag(IGNORE_WARNING))
		{
			show_warning();
		}
		else
		{
			set_flag(RECOMPILE);
			clear_flag(MODIFIED|FILENAME_SET|NEW|OPEN|SAVE|DIALOG|IGNORE_WARNING);
			filename = default_filename;
			editor.SetText("");
			editor.MoveTop(false);

			song_manager->reset_mute();
			song_manager->stop();
		}
	}
	// open dialog requested
	else if(test_flag(OPEN) && !modal_open)
	{
		if((flag & MODIFIED) && !(flag & IGNORE_WARNING))
		{
			show_warning();
		}
		else
		{
			modal_open = 1;
			ImVec2 dialog_size(700, 600);
			fs.chooseFileDialog(test_flag(DIALOG), fs.getLastDirectory(), default_filter, NULL, dialog_size);
			clear_flag(DIALOG);
			if(strlen(fs.getChosenPath()) > 0)
			{
				if(load_file(fs.getChosenPath()))
					set_flag(DIALOG);						// File couldn't be opened
				else
					clear_flag(OPEN|IGNORE_WARNING);
			}
			else if(fs.hasUserJustCancelledDialog())
			{
				clear_flag(OPEN|IGNORE_WARNING);
			}
		}
	}
	// save dialog requested
	else if(test_flag(SAVE) && !modal_open)
	{
		if(test_flag(SAVE_AS) || !test_flag(FILENAME_SET))
		{
			modal_open = 1;
			fs.saveFileDialog(test_flag(DIALOG), fs.getLastDirectory(), get_display_filename().c_str(), default_filter);
			clear_flag(DIALOG);
			if(strlen(fs.getChosenPath()) > 0)
			{
				if(save_file(fs.getChosenPath()))
					set_flag(DIALOG);						// File couldn't be saved
				else
					clear_flag(SAVE|SAVE_AS);
			}
			else if(fs.hasUserJustCancelledDialog())
			{
				clear_flag(SAVE|SAVE_AS);
			}
		}
		else
		{
			// TODO: show a message if file couldn't be saved ...
			clear_flag(DIALOG|SAVE);
			if(save_file(filename.c_str()))
				player_error = "File couldn't be saved.";
		}
	}
	// export dialog requested
	else if(test_flag(EXPORT) && !modal_open)
	{
		modal_open = 1;
		fs.saveFileDialog(test_flag(DIALOG), fs.getLastDirectory(), get_export_filename().c_str(), export_filter.c_str());
		clear_flag(DIALOG);
		if(strlen(fs.getChosenPath()) > 0)
		{
			export_file(fs.getChosenPath());
			clear_flag(EXPORT);
		}
		else if(fs.hasUserJustCancelledDialog())
		{
			clear_flag(EXPORT);
		}
	}
	else if(test_flag(IMPORT) && !modal_open)
	{
		modal_open = 1;
		fs.chooseFileDialog(test_flag(DIALOG), fs.getLastDirectory(), ".dmf");
		clear_flag(DIALOG);
		if(strlen(fs.getChosenPath()) > 0)
		{
			import_file(fs.getChosenPath());
			clear_flag(IMPORT);
		}
		else if(fs.hasUserJustCancelledDialog())
		{
			clear_flag(IMPORT);
		}
	}
}

int Editor_Window::load_file(const char* fn)
{
	auto t = std::ifstream(fn);
	if (t.good())
	{
		clear_flag(MODIFIED);
		set_flag(FILENAME_SET|RECOMPILE);
		filename = fn;
		std::string str((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());
		editor.SetText(str);
		editor.MoveTop(false);

		song_manager->stop();
		song_manager->reset_mute();
		return 0;
	}
	return -1;
}

int Editor_Window::import_file(const char* fn)
{
	auto t = Dmf_Importer(fn);
	player_error += t.get_error();
	if (!player_error.size())
	{
		clear_flag(MODIFIED);
		set_flag(FILENAME_SET|RECOMPILE);
		editor.InsertText(t.get_mml());
		return 0;
	}
	return -1;
}

int Editor_Window::save_file(const char* fn)
{
	// If we don't set ios::binary, the runtime will
	// convert linebreaks to the OS native format.
	// I think it is OK to keep this behavior for now.
	auto t = std::ofstream(fn);
	if (t.good())
	{
		clear_flag(MODIFIED);
		set_flag(FILENAME_SET|RECOMPILE);
		filename = fn;
		std::string str = editor.GetText();
		t.write((char*)str.c_str(), str.size());
		return 0;
	}
	return -1;
}

void Editor_Window::export_file(const char* fn)
{
	try
	{
		auto song = song_manager->get_song();
		auto bytes = song->get_platform()->get_export_data(*song, export_format);
		auto t = std::ofstream(fn, std::ios::binary);
		if(t.good())
			t.write((char*)bytes.data(), bytes.size());
		else
			player_error = "Cannot open file '" + std::string(fn) + "'";
	}
	catch(InputError& except)
	{
		player_error = except.what();
	}
	catch(std::exception& except)
	{
		player_error = "exception type: ";
		player_error += typeid(except).name();
		player_error += "\nexception message: ";
		player_error += except.what();
	}
}

std::string Editor_Window::get_display_filename() const
{
	auto pos = filename.rfind("/");
	if(pos != std::string::npos)
		return filename.substr(pos+1);
	else
		return filename;
}

std::string Editor_Window::get_export_filename() const
{
	auto spos = filename.rfind("/");
	auto epos = filename.rfind(".");
	if(spos != std::string::npos)
		return filename.substr(spos+1, epos-spos-1) + export_filter;
	else
		return filename.substr(0, epos) + export_filter;
}

// move this to top?
void Editor_Window::show_player_controls()
{
	auto result = song_manager->get_compile_result();

	ImGui::SameLine();
	float content = ImGui::GetContentRegionMax().x;
	//float offset = content * 0.4f;	// with progress bar
	float offset = content * 0.75f;		// without progress bar
	float width = content - offset;
	float curpos = ImGui::GetCursorPos().x;
	if(offset < curpos)
		offset = curpos;
	ImGui::SetCursorPosX(offset);

	// PushNextItemWidth doesn't work for some stupid reason, width must be set manually
	auto size = ImVec2(content * 0.1f, 0);
	if(size.x < ImGui::GetFontSize() * 4.0f)
		size.x = ImGui::GetFontSize() * 4.0f;

	// Handle play button (also indicates compile errors)
	if(result != Song_Manager::COMPILE_OK)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.6f);
		if(result == Song_Manager::COMPILE_NOT_DONE)
		{
			ImGui::Button("Wait", size);
		}
		else if(result == Song_Manager::COMPILE_ERROR)
		{
			ImGui::Button("Error", size);
			if (ImGui::IsItemHovered())
			{
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted(song_manager->get_error_message().c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
				ImGui::PopStyleVar();
			}
		}
		ImGui::PopStyleVar();
	}
	else
	{
		if(ImGui::Button("Play", size))
			play_song();
	}
	// Handle stop button
	ImGui::SameLine();
	if (ImGui::Button("Stop", size))
		stop_song();

/*
	// Handle a progress bar. Just dummy for now
	float bar_test = width / content;
	ImGui::SameLine();
	size = ImVec2(ImGui::GetContentRegionAvail().x, 0);
	ImGui::ProgressBar(bar_test, size);
*/
}

void Editor_Window::get_compile_result()
{
	ImGui::SameLine();
	switch(song_manager->get_compile_result())
	{
		default:
		case Song_Manager::COMPILE_NOT_DONE:
			ImGui::Text("Compiling");
			break;
		case Song_Manager::COMPILE_OK:
			ImGui::Text("OK");
			break;
		case Song_Manager::COMPILE_ERROR:
			ImGui::Text("Error");
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted(song_manager->get_error_message().c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
			break;
	}
}

std::string Editor_Window::dump_state()
{
	std::string str = "filename: " + filename + "\n";
	str += "modified: " + std::to_string(test_flag(MODIFIED)) + "\n";
	str += "contents:\n" + editor.GetText() + "\nend contents\n";
	return str;
}

//! Get the length of a subroutine (helper function for macro highlighting)
static unsigned int get_subroutine_length_helper(Song& song, unsigned int param, unsigned int max_recursion)
{
	try
	{
		Track& track = song.get_track(param);
		if(track.get_event_count())
		{
			auto event = track.get_event(track.get_event_count() - 1);
			uint32_t end_time;
			if(event.type == Event::JUMP && max_recursion != 0)
				end_time = event.play_time + get_subroutine_length_helper(song, event.param, max_recursion - 1);
			else if(event.type == Event::LOOP_END && max_recursion != 0)
			{
				// Simplified loop length calculation
				unsigned int loop_count = event.param - 1;
				unsigned int loop_start_time = 0;
				int depth = 0;
				for(unsigned int pos = track.get_event_count() - 1; pos > 0; pos--)
				{
					auto loop_event = track.get_event(pos);
					if(loop_event.type == Event::LOOP_END)
						depth++;
					else if(loop_event.type == Event::LOOP_START)
					{
						if(depth)
							depth--;
						else
						{
							loop_start_time = loop_event.play_time;
							break;
						}
					}
				}
				end_time = event.play_time + (event.play_time - loop_start_time) * loop_count;
			}
			else
				end_time = event.play_time + event.on_time + event.off_time;
			return end_time - track.get_event(0).play_time;
		}
	}
	catch(std::exception &e)
	{
	}
	return 0;
}

void Editor_Window::show_track_positions()
{
	std::map<int, std::unordered_set<int>> highlights = {};
	Song_Manager::Track_Map map = {};
	unsigned int ticks = 0;

	auto tracks = song_manager->get_tracks();
	if(tracks != nullptr)
		map = *tracks;

	auto player = song_manager->get_player();
	if(player != nullptr && !player->get_finished())
		ticks = player->get_driver()->get_player_ticks();

	auto song = song_manager->get_song();
	if(song == nullptr)
	{
		editor.SetMmlHighlights(highlights);
		return;
	}

	for(auto track_it = map.begin(); track_it != map.end(); track_it++)
	{
		auto& info = track_it->second;
		int offset = 0;

		// calculate offset to first loop
		if(ticks > info.length && info.loop_length)
			offset = ((ticks - info.loop_start) / info.loop_length) * info.loop_length;

		// calculate position
		auto it = info.events.lower_bound(ticks - offset);
		if(it != info.events.begin())
		{
			--it;
			auto event = it->second;
			for(auto && i : event.references)
			{
				// Include all references for highlighting - empty filename means current file,
				// and we want to highlight macro tracks and rndpat patterns even if they have filenames
				highlights[i->get_line()].insert(i->get_column());
			}
		}

		// Also check if we're inside a JUMP event (macro call) by examining the actual track
		// This handles cases where the Track_Info doesn't have events during macro execution
		try
		{
			Track& track = song->get_track(track_it->first);
			unsigned int event_count = track.get_event_count();
			
			// Find JUMP events that might be active at the current tick position
			for(unsigned int pos = 0; pos < event_count; pos++)
			{
				auto track_event = track.get_event(pos);
				if(track_event.type == Event::JUMP)
				{
					// Calculate if we're within this JUMP event's duration
					unsigned int jump_start = track_event.play_time;
					unsigned int jump_length = get_subroutine_length_helper(*song, track_event.param, 10);
					unsigned int jump_end = jump_start + jump_length;
					
					// Account for looping
					unsigned int local_ticks = ticks - offset;
					if(local_ticks >= jump_start && local_ticks < jump_end)
					{
						// We're inside a macro call - get events from the macro track
						unsigned int macro_offset = local_ticks - jump_start;
						
						// Check if the macro track is in our map
						auto macro_it = map.find(track_event.param);
						if(macro_it != map.end())
						{
							auto& macro_info = macro_it->second;
							int macro_offset_loop = 0;
							
							// Handle looping in macro track
							if(macro_offset > macro_info.length && macro_info.loop_length)
								macro_offset_loop = ((macro_offset - macro_info.loop_start) / macro_info.loop_length) * macro_info.loop_length;
							
							// Find events in the macro track
							auto macro_event_it = macro_info.events.lower_bound(macro_offset - macro_offset_loop);
							if(macro_event_it != macro_info.events.begin())
							{
								--macro_event_it;
								auto macro_event = macro_event_it->second;
								for(auto && ref : macro_event.references)
								{
									highlights[ref->get_line()].insert(ref->get_column());
								}
							}
						}
					}
				}
			}
		}
		catch(std::exception&)
		{
			// Track might not exist, skip it
		}
	}
	editor.SetMmlHighlights(highlights);
}

void Editor_Window::show_export_menu()
{
	auto format_list = song_manager->get_song()->get_platform()->get_export_formats();
	unsigned int id = 0;
	for(auto&& i : format_list)
	{
		std::string item_title = "Export " + i.second + "...";
		if (ImGui::MenuItem(item_title.c_str()))
		{
			set_flag(EXPORT|DIALOG);
			export_format = id;
			export_filter = "." + i.first;
		}
		id++;
	}
}

void Editor_Window::set_editor_palette(bool light_mode)
{
	if (light_mode)
		set_palette_with_theme_text(TextEditor::GetLightPalette(), true);
	else
		set_palette_with_theme_text(TextEditor::GetDarkPalette(), false);
}

void Editor_Window::set_palette_with_theme_text(const TextEditor::Palette& palette, bool light_mode)
{
	TextEditor::Palette modified_palette = palette;
	// Set Default text color based on theme:
	// White (0xffffffff) for dark mode, black (0xff000000) for light mode
	modified_palette[(int)TextEditor::PaletteIndex::Default] = light_mode ? 0xff000000 : 0xffffffff;
	editor.SetPalette(modified_palette);
}
