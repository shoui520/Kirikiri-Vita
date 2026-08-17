#pragma once

namespace TJS {
class tTJSString;
}

TJS::tTJSString krkrvita_yuri_select_project(
    const TJS::tTJSString& native_project_root);
void krkrvita_yuri_storage_preflight(const TJS::tTJSString& native_project_path);
void krkrvita_yuri_startup_storage_preflight();

// The staged game's Windows executable path, or an empty string when the
// project directory has no unambiguous executable. Kirikiri's System.exeName
// must be a file path because titles derive sibling resources from it.
TJS::tTJSString krkrvita_yuri_project_executable_path(
    const TJS::tTJSString& native_project_root);
