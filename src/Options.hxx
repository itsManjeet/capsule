#ifndef SRCLANG_OPTIONS_HXX
#define SRCLANG_OPTIONS_HXX

#include <variant>
#include <vector>
#include <string>
#include <map>

namespace srclang {
    using OptionType = std::variant<std::string, int, float, bool>;

    class Options : public std::map<std::string, OptionType> {
    public:
        explicit Options(std::map<std::string, OptionType> const &);
    };

} // srclang

#endif //SRCLANG_OPTIONS_HXX
