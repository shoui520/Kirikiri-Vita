#include "krkrvita/retail_bootstrap.hpp"

#include "DebugIntf.h"
#include "ScriptMgnIntf.h"
#include "StorageIntf.h"
#include "SysInitImpl.h"

#include <psp2/io/dirent.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

extern bool TVPEncodeUTF8ToUTF16(tjs_string &output, const std::string &source);

namespace {

void mount_patch_archives(const tjs_string &directory)
{
	const std::string native_directory = ttstr(directory).AsNarrowStdString();
	const SceUID handle = sceIoDopen(native_directory.c_str());
	if(handle < 0) return;

	std::vector<std::string> archives;
	SceIoDirent entry;
	while(true)
	{
		std::memset(&entry, 0, sizeof(entry));
		const int result = sceIoDread(handle, &entry);
		if(result <= 0) break;
		std::string name(entry.d_name);
		std::string lowered(name);
		std::transform(lowered.begin(), lowered.end(), lowered.begin(),
			[](unsigned char character) { return static_cast<char>(std::tolower(character)); });
		if(lowered.size() >= 4 && lowered.compare(lowered.size() - 4, 4, ".xp3") == 0)
			archives.push_back(name);
	}
	sceIoDclose(handle);

	// Later-numbered retail patches conventionally have higher priority.
	std::sort(archives.begin(), archives.end(), std::greater<std::string>());
	for(std::vector<std::string>::const_iterator it = archives.begin();
		it != archives.end(); ++it)
	{
		tjs_string wide_name;
		if(!TVPEncodeUTF8ToUTF16(wide_name, *it)) continue;
		tjs_string archive_path = directory;
		archive_path += wide_name;
		archive_path += TVPArchiveDelimiter;
		TVPAddAutoPath(ttstr(archive_path));
		TVPAddImportantLog(ttstr(TJS_W("Mounted retail compatibility archive: ")) +
			ttstr(archive_path));
	}
}

} // namespace

void krkrvita_execute_patch_script()
{
	tTJSVariant option;
	ttstr path;
	if(TVPGetCommandLine(TJS_W("-krkrpatch"), &option)) path = option;

	if(path.IsEmpty() && !TVPNativeProjectDir.empty())
	{
		tjs_string candidate = TVPNativeProjectDir;
		if(candidate.back() != TJS_W('/') && candidate.back() != TJS_W('\\'))
			candidate += TJS_W('/');
		candidate += TJS_W("patch.tjs");
		if(TVPIsExistentStorageNoSearch(candidate)) path = candidate;
	}
	if(path.IsEmpty())
	{
		const ttstr candidate = TVPProjectDir + TJS_W("patch.tjs");
		if(TVPIsExistentStorageNoSearch(candidate)) path = candidate;
	}
	if(path.IsEmpty()) return;

	const tjs_string path_string = path.AsStdString();
	const tjs_string::size_type separator = path_string.find_last_of(TJS_W("/\\"));
	if(separator != tjs_string::npos)
	{
		const tjs_string patch_directory = path_string.substr(0, separator + 1);
		TVPAddAutoPath(ttstr(patch_directory));
		mount_patch_archives(patch_directory);
	}
	TVPAddImportantLog(ttstr(TJS_W("Executing retail compatibility patch: ")) + path);
	TVPExecuteStorage(path);
}
