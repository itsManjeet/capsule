#ifndef SRCLANG_UTILITIES_HXX
#define SRCLANG_UTILITIES_HXX

#include <string>
#include <ostream>
#include <istream>


namespace srclang {
    template<class V>
    std::type_info const &variant_typeid(V const &v) {
        return visit([](auto &&x) -> decltype(auto) { return typeid(x); }, v);
    }
} // srclang

#endif //SRCLANG_UTILITIES_HXX
