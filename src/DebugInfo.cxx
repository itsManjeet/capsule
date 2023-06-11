#include "DebugInfo.hxx"
#include "Utilities.hxx"

namespace srclang {
    void DebugInfo::dump(std::ostream &os) {
        dump_string(filename, os);
        dump_int<int>(position, os);
        dump_int<size_t>(lines.size(), os);
        for (auto i: lines) {
            dump_int<int>(i, os);
        }
    }

    DebugInfo *DebugInfo::read(std::istream &is) {
        auto debug_info = new DebugInfo();
        debug_info->filename = read_string(is);
        debug_info->position = read_int<int>(is);
        auto size = read_int<size_t>(is);
        for (auto i = 0; i < size; i++) {
            debug_info->lines.push_back(read_int<int>(is));
        }
        return debug_info;
    }
} // srclang