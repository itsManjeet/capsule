#include "Value.hxx"
#include "MemoryManager.hxx"

using namespace srclang;

#include <stdexcept>
#include <sstream>

using namespace std;

ValueType srclang::SRCLANG_VALUE_GET_TYPE(Value val) {
    if (SRCLANG_VALUE_IS_NULL(val)) return ValueType::Null;
    if (SRCLANG_VALUE_IS_BOOL(val)) return ValueType::Boolean;
    if (SRCLANG_VALUE_IS_DECIMAL(val)) return ValueType::Decimal;
    if (SRCLANG_VALUE_IS_CHAR(val)) return ValueType::Char;
    if (SRCLANG_VALUE_IS_TYPE(val)) return ValueType::Type;
    if (SRCLANG_VALUE_IS_INTEGER(val)) return ValueType::Integer;

    if (SRCLANG_VALUE_IS_OBJECT(val))
        return (SRCLANG_VALUE_AS_OBJECT(val)->type);
    throw runtime_error("invalid value '" + to_string((uint64_t) val) + "'");
}

string srclang::SRCLANG_VALUE_GET_STRING(Value val) {
    auto type = SRCLANG_VALUE_GET_TYPE(val);
    switch (type) {
        case ValueType::Null:
            return "null";
        case ValueType::Boolean:
            return SRCLANG_VALUE_AS_BOOL(val) ? "true" : "false";
        case ValueType::Decimal:
            return to_string(SRCLANG_VALUE_AS_DECIMAL(val));
        case ValueType::Integer:
            return to_string(SRCLANG_VALUE_AS_INTEGER(val));
        case ValueType::Char:
            return string(1, (char) SRCLANG_VALUE_AS_CHAR(val));
        case ValueType::Type:
            return "<type(" +
                   SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_AS_TYPE(val))] +
                   ")>";
        default:
            if (SRCLANG_VALUE_IS_OBJECT(val)) {
                auto object = SRCLANG_VALUE_AS_OBJECT(val);
                switch (type) {
                    case ValueType::String:
                    case ValueType::Error:
                        return (char *) object->pointer;
                    case ValueType::List: {
                        stringstream ss;
                        ss << "[";
                        string sep;
                        for (auto const &i: *(reinterpret_cast<SrcLangList *>(object->pointer))) {
                            ss << sep << SRCLANG_VALUE_GET_STRING(i);
                            sep = ", ";
                        }
                        ss << "]";
                        return ss.str();
                    }
                        break;

                    case ValueType::Map: {
                        stringstream ss;
                        ss << "{";
                        string sep;
                        for (auto const &i: *(reinterpret_cast<SrcLangMap *>(object->pointer))) {
                            ss << sep << i.first << ":" << SRCLANG_VALUE_GET_STRING(i.second);
                            sep = ", ";
                        }
                        ss << "}";
                        return ss.str();
                    }
                        break;

                    case ValueType::Function: {
                        return "<function()>";
                    }
                        break;

                    case ValueType::Pointer: {
                        stringstream ss;
                        ss << "0x" << std::hex << reinterpret_cast<unsigned long>(object->pointer);
                        return ss.str();
                    }

                    default:
                        return "<object(" + SRCLANG_VALUE_TYPE_ID[int(type)] + ")>";
                }
            }
    }

    return "<value(" + SRCLANG_VALUE_TYPE_ID[int(type)] + ")>";
}
