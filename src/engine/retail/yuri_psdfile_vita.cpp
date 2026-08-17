#include "psdparse.h"
#include "psdfile.h"

#include <fstream>
#include <limits>

namespace psd {

// Boost's mapped_file_source implementation is not part of the Vita SDK
// archive (the SDK object is deliberately empty).  The PSD parser retains
// iterators into its input for deferred layer decoding, so keep a private
// byte vector on PSDFile and preserve that lifetime exactly as the mapped file
// did on desktop.
bool PSDFile::load(const char *filename) {
    clearData();
    isLoaded = false;
    in.clear();

    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file)
        return false;
    const std::streamoff end = file.tellg();
    if (end <= 0 || static_cast<unsigned long long>(end) >
                         static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max()))
        return false;

    in.resize(static_cast<std::size_t>(end));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char *>(in.data()),
              static_cast<std::streamsize>(in.size()));
    if (!file || static_cast<std::size_t>(file.gcount()) != in.size()) {
        in.clear();
        return false;
    }

    using iterator_type = const unsigned char *;
    iterator_type iter = in.data();
    iterator_type finish = iter + in.size();
    psd::Parser<iterator_type> parser(*this);
    const bool parsed = parse(iter, finish, parser);
    if (parsed && iter == finish) {
        isLoaded = processParsed();
    }
    if (!isLoaded)
        in.clear();
    return isLoaded;
}

} // namespace psd
