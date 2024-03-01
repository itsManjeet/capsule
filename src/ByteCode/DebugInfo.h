#ifndef SRCLANG_DEBUGINFO_H
#define SRCLANG_DEBUGINFO_H

#include <iostream>
#include <string>
#include <vector>

namespace srclang {

    struct DebugInfo {
        std::string filename;
        std::vector<int> lines;
        int position{};
    };

}  // srclang

#endif  // SRCLANG_DEBUGINFO_H
