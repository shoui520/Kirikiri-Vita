#include "krkrvita/retail_bootstrap.hpp"
#include "krkrvita/game.hpp"
#include "krkrvita/patch_manifest.hpp"
#include "krkrvita/phase1_filter.hpp"

#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::vector<std::string> launch_arguments;
std::vector<char *> launch_argument_pointers;
bool launch_error_reported = false;

void report_launch_error(const std::string &message);

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

struct EmbeddedPatch
{
	std::string filter;
	std::string startup;
	std::string error;
	bool automatic = false;
	bool failed = false;
};

std::string repository_patch_path(const std::string &reference)
{
	const std::string shared_prefix("../patch/");
	if(reference.compare(0, 8, "https://") == 0) return std::string();
	std::string relative = reference;
	if(relative.compare(0, shared_prefix.size(), shared_prefix) == 0)
		relative.erase(0, shared_prefix.size());
	if(!krkrvita::is_safe_patch_path(relative))
		return std::string();
	return relative;
}

void ensure_directory_tree(const std::string &path)
{
	const std::string::size_type device = path.find(':');
	std::string::size_type slash = path.find('/',
		device == std::string::npos ? 0 : device + 1);
	while(slash != std::string::npos)
	{
		if(slash > 0) sceIoMkdir(path.substr(0, slash).c_str(), 0777);
		slash = path.find('/', slash + 1);
	}
	sceIoMkdir(path.c_str(), 0777);
}

void write_text_atomic(const std::string &destination, const std::string &text)
{
	const std::string::size_type slash = destination.find_last_of('/');
	if(slash != std::string::npos)
		ensure_directory_tree(destination.substr(0, slash));
	const std::string temporary = destination + ".part";
	sceIoRemove(temporary.c_str());
	const SceUID output = sceIoOpen(temporary.c_str(),
		SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
	if(output < 0)
		throw std::runtime_error("cannot create generated Phase 1 filter");

	std::size_t written = 0;
	bool okay = true;
	while(written < text.size())
	{
		const std::size_t remaining = text.size() - written;
		const unsigned int amount = static_cast<unsigned int>(
			std::min<std::size_t>(remaining, 64 * 1024));
		const int result = sceIoWrite(output, text.data() + written, amount);
		if(result <= 0)
		{
			okay = false;
			break;
		}
		written += static_cast<std::size_t>(result);
	}
	if(okay && sceIoSyncByFd(output, 0) < 0) okay = false;
	sceIoClose(output);
	if(!okay)
	{
		sceIoRemove(temporary.c_str());
		throw std::runtime_error("cannot write generated Phase 1 filter");
	}
	sceIoRemove(destination.c_str());
	if(sceIoRename(temporary.c_str(), destination.c_str()) < 0)
	{
		sceIoRemove(temporary.c_str());
		throw std::runtime_error("cannot commit generated Phase 1 filter");
	}
}

struct Phase1Filter
{
	std::string path;
};

Phase1Filter materialize_phase1_filter(const std::string &game_path)
{
	krkrvita_boot_trace("retail-phase1-heuristic-entered");
	std::string error;
	const std::optional<krkrvita::Phase1FilterResult> inferred =
		krkrvita::infer_phase1_filter(game_path, &error);
	if(!inferred) throw std::runtime_error(error.empty()
		? "Phase 1 inference did not return a rule" : error);
	const std::string destination =
		"ux0:data/krkrvita/heuristics/" + inferred->archive_fingerprint +
		"/xp3filter.tjs";
	write_text_atomic(destination, inferred->script);
	krkrvita_boot_trace("retail-phase1-heuristic-verified");
	if(inferred->rule_name == "identity")
		krkrvita_boot_trace("retail-phase1-heuristic-identity");
	krkrvita_boot_trace("retail-phase1-heuristic-selected");
	return Phase1Filter{destination};
}

void extract_archive_file(struct archive *reader, const std::string &destination,
	la_int64_t expected_size)
{
	const std::string::size_type slash = destination.find_last_of('/');
	if(slash != std::string::npos)
		ensure_directory_tree(destination.substr(0, slash));
	const std::string temporary = destination + ".part";
	sceIoRemove(temporary.c_str());
	const SceUID output = sceIoOpen(temporary.c_str(),
		SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
	if(output < 0)
		throw std::runtime_error("cannot create retail patch cache file");

	std::vector<unsigned char> buffer(64 * 1024);
	bool okay = true;
	la_int64_t total = 0;
	while(true)
	{
		const la_ssize_t read = archive_read_data(reader,
			buffer.data(), buffer.size());
		if(read == 0) break;
		if(read < 0)
		{
			okay = false;
			break;
		}
		total += read;
		la_ssize_t written = 0;
		while(written < read)
		{
			const int result = sceIoWrite(output, buffer.data() + written,
				static_cast<unsigned int>(read - written));
			if(result <= 0)
			{
				okay = false;
				break;
			}
			written += result;
		}
		if(!okay) break;
	}
	if(expected_size >= 0 && total != expected_size) okay = false;
	if(okay && sceIoSyncByFd(output, 0) < 0) okay = false;
	sceIoClose(output);
	if(!okay)
	{
		sceIoRemove(temporary.c_str());
		throw std::runtime_error("cannot extract retail patch cache file");
	}
	sceIoRemove(destination.c_str());
	if(sceIoRename(temporary.c_str(), destination.c_str()) < 0)
	{
		sceIoRemove(temporary.c_str());
		throw std::runtime_error("cannot commit retail patch cache file");
	}
}

std::map<std::string, std::string> materialize_embedded_files(
	const krkrvita::PatchEntry &entry)
{
	const std::string cache_root = "ux0:data/krkrvita/patch-cache/" +
		std::string(krkrvita::kPatchCommit);
	std::map<std::string, std::string> wanted;
	for(const std::string &reference : entry.files)
	{
		const std::string relative = repository_patch_path(reference);
		if(relative.empty()) continue;
		wanted["patch/" + relative] = join_path(cache_root, relative);
	}
	if(wanted.empty()) return {};

	using ArchivePointer = std::unique_ptr<struct archive, decltype(&archive_read_free)>;
	ArchivePointer reader(archive_read_new(), &archive_read_free);
	if(!reader) throw std::runtime_error("cannot allocate retail patch reader");
	archive_read_support_filter_all(reader.get());
	archive_read_support_format_zip(reader.get());
	if(archive_read_open_filename(reader.get(),
		"app0:krkrvita/patches/patches.zip", 64 * 1024) != ARCHIVE_OK)
		throw std::runtime_error("cannot open embedded retail patch bundle");

	std::size_t resolved = 0;
	struct archive_entry *archive_entry = nullptr;
	while(resolved < wanted.size() &&
		archive_read_next_header(reader.get(), &archive_entry) == ARCHIVE_OK)
	{
		const char *pathname = archive_entry_pathname(archive_entry);
		const auto target = pathname ? wanted.find(pathname) : wanted.end();
		if(target == wanted.end())
		{
			archive_read_data_skip(reader.get());
			continue;
		}
		const la_int64_t size = archive_entry_size(archive_entry);
		if(size < 0 || size > 128 * 1024 * 1024)
			throw std::runtime_error("embedded retail patch file is too large");
		SceIoStat status{};
		const bool cached = sceIoGetstat(target->second.c_str(), &status) >= 0 &&
			SCE_S_ISREG(status.st_mode) && status.st_size == size;
		if(cached)
			archive_read_data_skip(reader.get());
		else
			extract_archive_file(reader.get(), target->second, size);
		++resolved;
	}

	if(resolved != wanted.size())
		throw std::runtime_error("embedded retail patch entry is incomplete");
	return wanted;
}

EmbeddedPatch resolve_embedded_patch(const std::string &game_path)
{
	EmbeddedPatch result;
	try
	{
		const krkrvita::GameDescriptor game =
			krkrvita::GameScanner::scan(std::filesystem::path(game_path));
		const krkrvita::PatchManifest manifest = krkrvita::PatchManifest::load(
			std::filesystem::path("app0:krkrvita/patches/alldata.js"));
		const krkrvita::PatchResolution resolution =
			krkrvita::PatchResolver::resolve(game, manifest);
		if(!resolution.automatic || !resolution.best()) return result;
		result.automatic = true;
		const std::map<std::string, std::string> files =
			materialize_embedded_files(*resolution.best()->entry);
		for(const std::string &reference : resolution.best()->entry->files)
		{
			const std::string relative = repository_patch_path(reference);
			const auto found = files.find("patch/" + relative);
			if(relative.empty() || found == files.end()) continue;
			const std::string &path = found->second;
			const std::string::size_type slash = reference.find_last_of("/\\");
			const std::string leaf = slash == std::string::npos
				? reference : reference.substr(slash + 1);
			if(leaf == "xp3filter.tjs") result.filter = path;
			else if(leaf == "patch.tjs") result.startup = path;
		}
	}
	catch(const std::exception &exception)
	{
		result.failed = true;
		result.error = exception.what();
		krkrvita_boot_trace("retail-patch-resolution-failed");
	}
	catch(...)
	{
		result.failed = true;
		krkrvita_boot_trace("retail-patch-resolution-failed");
	}
	return result;
}

void append_retail_patch_arguments(const std::string &game_path,
	bool select_filter = true, bool select_patch = true,
	bool force_phase1_filter = false)
{
	const std::string local_filter = join_path(game_path, "xp3filter.tjs");
	const std::string local_patch = join_path(game_path, "patch.tjs");
	// Exact manifest selection is the primary retail path unless the profile
	// explicitly requests Phase 1. Files beside the game remain compatible,
	// but an unknown title is never assigned a guessed catch-all transform:
	// archive evidence must produce and verify a concrete rule.
	const EmbeddedPatch embedded = !force_phase1_filter &&
		(select_filter || select_patch)
		? resolve_embedded_patch(game_path) : EmbeddedPatch{};
	if(embedded.failed)
	{
		const bool local_can_continue =
			(!select_filter || file_exists(local_filter)) &&
			(!select_patch || file_exists(local_patch));
		if(!local_can_continue)
		{
			report_launch_error("Automatic retail patch extraction failed: " +
				(embedded.error.empty() ? std::string("unknown error") : embedded.error));
			return;
		}
		krkrvita_boot_trace("retail-patch-game-directory-fallback");
	}

	bool filter_selected = false;
	if(select_filter && force_phase1_filter)
	{
		try
		{
			const Phase1Filter inferred = materialize_phase1_filter(game_path);
			launch_arguments.push_back("-xp3filter=" + inferred.path);
			filter_selected = true;
		}
		catch(const std::exception &exception)
		{
			report_launch_error("Phase 1 XP3 filter inference failed: " +
				std::string(exception.what()));
			return;
		}
	}
	else if(select_filter && !embedded.filter.empty())
	{
		launch_arguments.push_back("-xp3filter=" + embedded.filter);
		krkrvita_boot_trace("retail-xp3filter-embedded-selected");
		filter_selected = true;
	}
	else if(select_filter && file_exists(local_filter))
	{
		launch_arguments.push_back("-xp3filter=" + local_filter);
		filter_selected = true;
	}
	else if(select_filter)
	{
		try
		{
			const Phase1Filter inferred = materialize_phase1_filter(game_path);
			launch_arguments.push_back("-xp3filter=" + inferred.path);
			filter_selected = true;
		}
		catch(const std::exception &exception)
		{
			report_launch_error("No exact xp3filter.tjs was found and Phase 1 failed: " +
				std::string(exception.what()));
			return;
		}
	}
	if(filter_selected) krkrvita_boot_trace("retail-xp3filter-selected");

	bool patch_selected = false;
	if(select_patch && !embedded.startup.empty())
	{
		launch_arguments.push_back("-krkrpatch=" + embedded.startup);
		krkrvita_boot_trace("retail-startup-patch-embedded-selected");
		patch_selected = true;
	}
	else if(select_patch && file_exists(local_patch))
	{
		launch_arguments.push_back("-krkrpatch=" + local_patch);
		patch_selected = true;
	}
	if(patch_selected) krkrvita_boot_trace("retail-startup-patch-selected");
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
		append_retail_patch_arguments(fallback_game);
		launch_arguments.push_back(fallback_game);
	}
	else
	{
		const std::map<std::string, std::string> values = read_profile(profile_path);
		const std::map<std::string, std::string>::const_iterator game = values.find("game_path");
		if(game == values.end() || game->second.empty())
		{
			report_launch_error("Profile has no game_path.");
			return;
		}

		launch_arguments.push_back(argc > 0 && argv[0] ? argv[0] : "app0:eboot.bin");
		const std::map<std::string, std::string>::const_iterator filter_mode =
			values.find("filter_mode");
		const bool force_phase1_filter = filter_mode != values.end() &&
			(filter_mode->second == "phase1" || filter_mode->second == "heuristic");
		const std::map<std::string, std::string>::const_iterator filter = values.find("xp3_filter_path");
		const bool explicit_filter = !force_phase1_filter && filter != values.end() &&
			!filter->second.empty() && file_exists(filter->second);
		if(explicit_filter)
		{
			launch_arguments.push_back("-xp3filter=" + filter->second);
			krkrvita_boot_trace("retail-xp3filter-selected");
		}
		std::map<std::string, std::string>::const_iterator patch = values.find("patch_script_path");
		std::string patch_path = patch == values.end() ? std::string() : patch->second;
		if(patch_path.empty())
		{
			patch = values.find("patch_root");
			if(patch != values.end()) patch_path = join_path(patch->second, "patch.tjs");
		}
		const bool explicit_patch = !patch_path.empty() && file_exists(patch_path);
		if(explicit_patch)
		{
			launch_arguments.push_back("-krkrpatch=" + patch_path);
			krkrvita_boot_trace("retail-startup-patch-selected");
		}
		if(!explicit_filter || !explicit_patch)
			append_retail_patch_arguments(game->second,
				!explicit_filter, !explicit_patch, force_phase1_filter);
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
