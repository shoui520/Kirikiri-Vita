#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cocos2d {

class Data {
public:
    const std::uint8_t* getBytes() const {
        return bytes_.empty() ? nullptr : bytes_.data();
    }
    std::size_t getSize() const { return bytes_.size(); }

private:
    friend class FileUtils;
    std::vector<std::uint8_t> bytes_;
};

// FontImpl's Android fallback asks Cocos for an application asset.  Vita font
// registration is provided separately, so an unresolved Cocos asset is the
// correct result at this backend boundary.
class FileUtils {
public:
    static FileUtils* getInstance() {
        static FileUtils instance;
        return &instance;
    }

    Data getDataFromFile(const std::string&) const { return {}; }
    std::string fullPathForFilename(const std::string&) const { return {}; }
};

} // namespace cocos2d
