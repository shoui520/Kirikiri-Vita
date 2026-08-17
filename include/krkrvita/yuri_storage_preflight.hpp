#pragma once

namespace TJS {
class tTJSString;
}

TJS::tTJSString krkrvita_yuri_select_project(
    const TJS::tTJSString& native_project_root);
void krkrvita_yuri_storage_preflight(const TJS::tTJSString& native_project_path);
void krkrvita_yuri_startup_storage_preflight();
