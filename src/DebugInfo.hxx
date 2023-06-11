#ifndef SRCLANG_DEBUGINFO_HXX
#define SRCLANG_DEBUGINFO_HXX

#include <string>
#include <vector>
#include <iostream>

namespace srclang {

    struct DebugInfo {
        std::string filename;
        std::vector<int> lines;
        int position{};

        void dump(std::ostream &os);

        static DebugInfo *read(std::istream &is);
    };

} // srclang

#endif //SRCLANG_DEBUGINFO_HXX
