#ifndef SRCLANG_DEBUGINFO_H
#define SRCLANG_DEBUGINFO_H

#include <iostream>
#include <string>
#include <vector>

namespace SrcLang {

struct DebugInfo {
    std::wstring filename;
    std::vector<int> lines;
    int position{};
};

} // namespace SrcLang

#endif // SRCLANG_DEBUGINFO_H
