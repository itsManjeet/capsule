#ifndef SRCLANG_UTILITIES_HXX
#define SRCLANG_UTILITIES_HXX

#include <string>
#include <ostream>
#include <istream>


namespace srclang {

    template<typename T>
    static inline void dump_int(T size, std::ostream &os) {
        os.write(reinterpret_cast<const char *>(&size), sizeof(T));
    }


    static inline void dump_string(const std::string &id, std::ostream &os) {
        size_t size = id.size();
        dump_int<size_t>(size, os);
        os.write(id.c_str(), size * sizeof(char));
    }

    template<typename T>
    static inline T read_int(std::istream &is) {
        T size;
        is.read(reinterpret_cast<char *>(&size), sizeof(T));
        return size;
    }


    static inline std::string read_string(std::istream &is) {
        auto size = read_int<size_t>(is);
        char buffer[size + 1];
        is.read(buffer, (int) size);
        buffer[size] = '\0';
        return buffer;
    }

    template<class V>
    std::type_info const &variant_typeid(V const &v) {
        return visit([](auto &&x) -> decltype(auto) { return typeid(x); }, v);
    }
} // srclang

#endif //SRCLANG_UTILITIES_HXX
