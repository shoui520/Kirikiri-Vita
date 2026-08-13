#include "tjsCommHead.h"

#include "DebugIntf.h"
#include "StorageIntf.h"
#include "SysInitImpl.h"
#include "TextStream.h"

// Implemented by Yuri's xp3filter subsystem, compiled unchanged apart from
// selecting its thread-local decoder path on this platform.
void TVPSetXP3FilterScript(ttstr content);

namespace {

bool read_filter(const ttstr &path, ttstr &content)
{
	if(path.IsEmpty() || !TVPIsExistentStorageNoSearch(path)) return false;
	iTJSTextReadStream *stream = TVPCreateTextStreamForRead(path, TJS_W(""));
	try
	{
		stream->Read(content, 0);
	}
	catch(...)
	{
		stream->Destruct();
		throw;
	}
	stream->Destruct();
	return !content.IsEmpty();
}

} // namespace

void krkrvita_install_xp3filter()
{
	tTJSVariant option;
	ttstr path;
	if(TVPGetCommandLine(TJS_W("-xp3filter"), &option))
	{
		path = option;
	}

	ttstr content;
	if(path.IsEmpty() && !TVPNativeProjectDir.empty())
	{
		tjs_string candidate = TVPNativeProjectDir;
		if(candidate.back() != TJS_W('/') && candidate.back() != TJS_W('\\'))
			candidate += TJS_W('/');
		candidate += TJS_W("xp3filter.tjs");
		path = candidate;
	}

	if(!read_filter(path, content))
	{
		path = TVPProjectDir + TJS_W("xp3filter.tjs");
		read_filter(path, content);
	}
	if(!content.IsEmpty())
	{
		TVPSetXP3FilterScript(content);
		TVPAddImportantLog(ttstr(TJS_W("Loaded retail XP3 filter: ")) + path);
	}
	else
	{
		TVPAddImportantLog(TJS_W("No xp3filter.tjs selected; encrypted retail archives cannot be read"));
	}
}
