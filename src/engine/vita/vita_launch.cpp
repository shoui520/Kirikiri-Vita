#include "krkrvita/retail_bootstrap.hpp"

#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

std::vector<std::string> launch_arguments;
std::vector<char *> launch_argument_pointers;
bool launch_error_reported = false;

std::string trim(std::string value)
{
	const std::string::size_type first = value.find_first_not_of(" \t\r\n");
	if(first == std::string::npos) return std::string();
	const std::string::size_type last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, last - first + 1);
}

std::string requested_game_id(int argc, char **argv)
{
	for(int i = 1; i < argc; ++i)
	{
		if(!argv[i]) continue;
		const std::string argument(argv[i]);
		const std::string short_prefix("game=");
		const std::string long_prefix("-krkrgame=");
		std::string value;
		if(argument.compare(0, short_prefix.size(), short_prefix) == 0)
			value = argument.substr(short_prefix.size());
		else if(argument.compare(0, long_prefix.size(), long_prefix) == 0)
			value = argument.substr(long_prefix.size());
		if(value.empty() || value.size() > 64) continue;
		if(std::find_if(value.begin(), value.end(), [](unsigned char character) {
			return !std::isalnum(character) && character != '-' && character != '_';
		}) == value.end()) return value;
	}
	return std::string();
}

bool has_explicit_project(int argc, char **argv)
{
	for(int i = 1; i < argc; ++i)
	{
		if(!argv[i] || argv[i][0] == '-') continue;
		if(std::string(argv[i]).compare(0, 5, "game=") == 0) continue;
		return true;
	}
	return false;
}

std::map<std::string, std::string> read_profile(const std::string &path)
{
	std::map<std::string, std::string> values;
	std::ifstream stream(path.c_str());
	std::string line;
	while(std::getline(stream, line))
	{
		line = trim(line);
		if(line.empty() || line[0] == '#' || line[0] == ';') continue;
		const std::string::size_type separator = line.find('=');
		if(separator == std::string::npos) continue;
		values[trim(line.substr(0, separator))] = trim(line.substr(separator + 1));
	}
	return values;
}

bool file_exists(const std::string &path)
{
	std::ifstream stream(path.c_str(), std::ios::binary);
	return static_cast<bool>(stream);
}

std::string join_path(const std::string &directory, const std::string &leaf)
{
	if(directory.empty()) return std::string();
	if(directory[directory.size() - 1] == '/' || directory[directory.size() - 1] == '\\')
		return directory + leaf;
	return directory + "/" + leaf;
}

bool directory_exists(const std::string &path)
{
	SceIoStat status;
	std::memset(&status, 0, sizeof(status));
	return sceIoGetstat(path.c_str(), &status) >= 0 && SCE_S_ISDIR(status.st_mode);
}

std::string discover_single_game()
{
	const std::string root = "ux0:data/krkrvita/games";
	const SceUID handle = sceIoDopen(root.c_str());
	if(handle < 0) return std::string();
	std::vector<std::string> directories;
	SceIoDirent entry;
	while(true)
	{
		std::memset(&entry, 0, sizeof(entry));
		const int result = sceIoDread(handle, &entry);
		if(result <= 0) break;
		if(!SCE_S_ISDIR(entry.d_stat.st_mode)) continue;
		const std::string name(entry.d_name);
		if(name == "." || name == "..") continue;
		directories.push_back(join_path(root, name));
		if(directories.size() > 1) break;
	}
	sceIoDclose(handle);
	return directories.size() == 1 ? directories.front() : std::string();
}

void report_launch_error(const std::string &message)
{
	launch_error_reported = true;
	krkrvita_write_error(message.c_str());
	krkrvita_boot_trace("fatal-error-written");
}

} // namespace

void krkrvita_report_launch_error(const char *message)
{
	report_launch_error(message ? message : "Unknown startup error");
}

bool krkrvita_launch_error_reported()
{
	return launch_error_reported;
}

void krkrvita_resolve_launch(int &argc, char **&argv)
{
	if(has_explicit_project(argc, argv)) return;

	const std::string game_id = requested_game_id(argc, argv);
	std::string profile_path = game_id.empty()
		? "ux0:data/krkrvita/active.ini"
		: "ux0:data/krkrvita/profiles/" + game_id + ".ini";
	if(!file_exists(profile_path))
	{
		std::string fallback_game = "ux0:data/krkrvita/game";
		if(!directory_exists(fallback_game)) fallback_game = discover_single_game();
		if(fallback_game.empty())
		{
			report_launch_error("No profile and no single game directory found. See error.txt.");
			return;
		}
		launch_arguments.push_back(argc > 0 && argv[0] ? argv[0] : "app0:eboot.bin");
		if(file_exists(join_path(fallback_game, "xp3filter.tjs")))
			launch_arguments.push_back("-xp3filter=" + join_path(fallback_game, "xp3filter.tjs"));
		else if(file_exists("app0:krkrvita/default-xp3filter.tjs"))
			launch_arguments.push_back("-xp3filter=app0:krkrvita/default-xp3filter.tjs");
		else
			report_launch_error("Selected game has no xp3filter.tjs.");
		if(file_exists(join_path(fallback_game, "patch.tjs")))
			launch_arguments.push_back("-krkrpatch=" + join_path(fallback_game, "patch.tjs"));
		launch_arguments.push_back(fallback_game);
	}
	else
	{
		const std::map<std::string, std::string> values = read_profile(profile_path);
		const std::map<std::string, std::string>::const_iterator game = values.find("game_path");
		if(game == values.end() || game->second.empty()) return;

		launch_arguments.push_back(argc > 0 && argv[0] ? argv[0] : "app0:eboot.bin");
		const std::map<std::string, std::string>::const_iterator filter = values.find("xp3_filter_path");
		if(filter != values.end() && !filter->second.empty() && file_exists(filter->second))
			launch_arguments.push_back("-xp3filter=" + filter->second);
		else
			report_launch_error("Profile xp3filter.tjs is missing.");
		std::map<std::string, std::string>::const_iterator patch = values.find("patch_script_path");
		std::string patch_path = patch == values.end() ? std::string() : patch->second;
		if(patch_path.empty())
		{
			patch = values.find("patch_root");
			if(patch != values.end()) patch_path = join_path(patch->second, "patch.tjs");
		}
		if(!patch_path.empty() && file_exists(patch_path))
			launch_arguments.push_back("-krkrpatch=" + patch_path);
		launch_arguments.push_back("-krkrprofile=" + profile_path);
		launch_arguments.push_back(game->second);
	}

	launch_argument_pointers.clear();
	for(std::vector<std::string>::iterator it = launch_arguments.begin();
		it != launch_arguments.end(); ++it)
		launch_argument_pointers.push_back(&(*it)[0]);
	argc = static_cast<int>(launch_argument_pointers.size());
	argv = launch_argument_pointers.data();
}
