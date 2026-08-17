#include "tjsCommHead.h"

#include "ConfigManager/GlobalConfigManager.h"
#include "ConfigManager/IndividualConfigManager.h"
#include "ConfigManager/LocaleConfigManager.h"
#include "Platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

bool TVPWriteDataToFile(const ttstr& filepath, const void* data,
                        unsigned int length);

namespace {

std::string join_path(const std::string& directory, const char* leaf) {
    if (directory.empty()) return leaf;
    if (directory.back() == '/' || directory.back() == '\\')
        return directory + leaf;
    return directory + "/" + leaf;
}

std::string xml_unescape(std::string value) {
    const std::pair<const char*, const char*> entities[] = {
        {"&quot;", "\""}, {"&apos;", "'"}, {"&lt;", "<"},
        {"&gt;", ">"}, {"&amp;", "&"},
    };
    for (const auto& entity : entities) {
        std::string::size_type offset = 0;
        while ((offset = value.find(entity.first, offset)) != std::string::npos) {
            value.replace(offset, std::strlen(entity.first), entity.second);
            offset += std::strlen(entity.second);
        }
    }
    return value;
}

std::string xml_escape(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (char character : value) {
        switch (character) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '\"': result += "&quot;"; break;
        case '\'': result += "&apos;"; break;
        default: result += character; break;
        }
    }
    return result;
}

bool attribute(const std::string& tag, const char* name, std::string& value) {
    const std::string prefix = std::string(name) + "=\"";
    const std::string::size_type begin = tag.find(prefix);
    if (begin == std::string::npos) return false;
    const std::string::size_type content = begin + prefix.size();
    const std::string::size_type end = tag.find('\"', content);
    if (end == std::string::npos) return false;
    value = xml_unescape(tag.substr(content, end - content));
    return true;
}

template <typename Callback>
void each_tag(const std::string& document, const char* name, Callback callback) {
    const std::string prefix = std::string("<") + name;
    std::string::size_type cursor = 0;
    while ((cursor = document.find(prefix, cursor)) != std::string::npos) {
        const std::string::size_type end = document.find('>', cursor);
        if (end == std::string::npos) break;
        callback(document.substr(cursor, end - cursor + 1));
        cursor = end + 1;
    }
}

} // namespace

void iSysConfigManager::Initialize() {
    AllConfig.clear();
    CustomArguments.clear();
    KeyMap.clear();
    ConfigUpdated = false;

    std::ifstream input(GetFilePath(), std::ios::binary);
    if (!input) return;
    const std::string document((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    each_tag(document, "Item", [this](const std::string& tag) {
        std::string key;
        std::string value;
        if (attribute(tag, "key", key) && attribute(tag, "value", value))
            AllConfig[key] = value;
    });
    each_tag(document, "Custom", [this](const std::string& tag) {
        std::string key;
        std::string value;
        if (attribute(tag, "key", key) && attribute(tag, "value", value))
            CustomArguments.emplace_back(key, value);
    });
    each_tag(document, "KeyMap", [this](const std::string& tag) {
        std::string key;
        std::string value;
        if (attribute(tag, "key", key) && attribute(tag, "value", value)) {
            const int source = std::atoi(key.c_str());
            const int destination = std::atoi(value.c_str());
            if (source && destination) KeyMap[source] = destination;
        }
    });
}

void iSysConfigManager::SaveToFile() {
    if (!ConfigUpdated || GetFilePath().empty()) return;
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<GlobalPreference>\n";
    for (const auto& item : AllConfig)
        xml << "  <Item key=\"" << xml_escape(item.first) << "\" value=\""
            << xml_escape(item.second) << "\"/>\n";
    for (const auto& item : CustomArguments)
        xml << "  <Custom key=\"" << xml_escape(item.first) << "\" value=\""
            << xml_escape(item.second) << "\"/>\n";
    for (const auto& item : KeyMap)
        xml << "  <KeyMap key=\"" << item.first << "\" value=\""
            << item.second << "\"/>\n";
    xml << "</GlobalPreference>\n";
    const std::string serialized = xml.str();
    if (TVPWriteDataToFile(ttstr(GetFilePath()), serialized.data(),
                           static_cast<unsigned int>(serialized.size())))
        ConfigUpdated = false;
}

bool iSysConfigManager::IsValueExist(const std::string& name) {
    return AllConfig.find(name) != AllConfig.end();
}

template <>
bool iSysConfigManager::GetValue<bool>(const std::string& name,
                                       const bool& default_value) {
    return GetValue<int>(name, default_value ? 1 : 0) != 0;
}

template <>
int iSysConfigManager::GetValue<int>(const std::string& name,
                                     const int& default_value) {
    const auto found = AllConfig.find(name);
    if (found != AllConfig.end()) return std::atoi(found->second.c_str());
    SetValueInt(name, default_value);
    return default_value;
}

template <>
float iSysConfigManager::GetValue<float>(const std::string& name,
                                         const float& default_value) {
    const auto found = AllConfig.find(name);
    if (found != AllConfig.end()) return std::strtof(found->second.c_str(), nullptr);
    SetValueFloat(name, default_value);
    return default_value;
}

template <>
std::string iSysConfigManager::GetValue<std::string>(
    const std::string& name, const std::string& default_value) {
    const auto found = AllConfig.find(name);
    if (found != AllConfig.end()) return found->second;
    SetValue(name, default_value);
    return default_value;
}

void iSysConfigManager::SetValueInt(const std::string& name, int value) {
    AllConfig[name] = std::to_string(value);
    ConfigUpdated = true;
}

void iSysConfigManager::SetValueFloat(const std::string& name, float value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.9g", static_cast<double>(value));
    AllConfig[name] = text;
    ConfigUpdated = true;
}

void iSysConfigManager::SetValue(const std::string& name,
                                 const std::string& value) {
    AllConfig[name] = value;
    ConfigUpdated = true;
}

void iSysConfigManager::SetKeyMap(int source, int destination) {
    if (destination)
        KeyMap[source] = destination;
    else
        KeyMap.erase(source);
    ConfigUpdated = true;
}

std::vector<std::string> iSysConfigManager::GetCustomArgumentsForPush() {
    std::vector<std::string> result;
    result.reserve(CustomArguments.size());
    for (const auto& argument : CustomArguments)
        result.emplace_back("-" + argument.first + "=" + argument.second);
    return result;
}

GlobalConfigManager::GlobalConfigManager() { Initialize(); }

GlobalConfigManager* GlobalConfigManager::GetInstance() {
    static GlobalConfigManager instance;
    return &instance;
}

std::string GlobalConfigManager::GetFilePath() {
    return TVPGetInternalPreferencePath() + "GlobalPreference.xml";
}

IndividualConfigManager* IndividualConfigManager::GetInstance() {
    static IndividualConfigManager instance;
    return &instance;
}

std::string IndividualConfigManager::GetFilePath() { return CurrentPath; }

void IndividualConfigManager::Clear() {
    AllConfig.clear();
    CustomArguments.clear();
    KeyMap.clear();
    ConfigUpdated = false;
    CurrentPath.clear();
}

bool IndividualConfigManager::CheckExistAt(const std::string& folder) {
    std::ifstream input(join_path(folder, "Kirikiroid2Preference.xml"));
    return static_cast<bool>(input);
}

bool IndividualConfigManager::CreatePreferenceAt(const std::string& folder) {
    Clear();
    CurrentPath = join_path(folder, "Kirikiroid2Preference.xml");
    ConfigUpdated = true;
    return true;
}

bool IndividualConfigManager::UsePreferenceAt(const std::string& folder) {
    const std::string path = join_path(folder, "Kirikiroid2Preference.xml");
    if (CurrentPath == path) return true;
    Clear();
    std::ifstream input(path);
    if (!input) return false;
    CurrentPath = path;
    Initialize();
    return true;
}

template <>
bool IndividualConfigManager::GetValue<bool>(const std::string& name,
                                              const bool& default_value) {
    return iSysConfigManager::GetValue<bool>(
        name, GlobalConfigManager::GetInstance()->GetValue<bool>(
                  name, default_value));
}

template <>
int IndividualConfigManager::GetValue<int>(const std::string& name,
                                            const int& default_value) {
    return iSysConfigManager::GetValue<int>(
        name, GlobalConfigManager::GetInstance()->GetValue<int>(
                  name, default_value));
}

template <>
float IndividualConfigManager::GetValue<float>(const std::string& name,
                                                const float& default_value) {
    return iSysConfigManager::GetValue<float>(
        name, GlobalConfigManager::GetInstance()->GetValue<float>(
                  name, default_value));
}

template <>
std::string IndividualConfigManager::GetValue<std::string>(
    const std::string& name, const std::string& default_value) {
    return iSysConfigManager::GetValue<std::string>(
        name, GlobalConfigManager::GetInstance()->GetValue<std::string>(
                  name, default_value));
}

std::vector<std::string> IndividualConfigManager::GetCustomArgumentsForPush() {
    if (CustomArguments.empty())
        return GlobalConfigManager::GetInstance()->GetCustomArgumentsForPush();
    return iSysConfigManager::GetCustomArgumentsForPush();
}

LocaleConfigManager::LocaleConfigManager() = default;

LocaleConfigManager* LocaleConfigManager::GetInstance() {
    static LocaleConfigManager instance;
    return &instance;
}

std::string LocaleConfigManager::GetFilePath() { return {}; }

void LocaleConfigManager::Initialize(const std::string& language) {
    currentLangCode = language;
    AllConfig.clear();
}

const std::string& LocaleConfigManager::GetText(const std::string& id) {
    const auto found = AllConfig.find(id);
    if (found != AllConfig.end()) return found->second;
    return AllConfig.emplace(id, id).first->second;
}
