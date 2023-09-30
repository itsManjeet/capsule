#include "MemoryManager.hxx"
#include "Value.hxx"

using namespace srclang;

#include <sstream>
#include <stdexcept>

using namespace std;

ValueType srclang::SRCLANG_VALUE_GET_TYPE(Value val) {
    if (SRCLANG_VALUE_IS_NULL(val)) return ValueType::Null;
    if (SRCLANG_VALUE_IS_BOOL(val)) return ValueType::Boolean;
    if (SRCLANG_VALUE_IS_NUMBER(val)) return ValueType::Number;
    if (SRCLANG_VALUE_IS_TYPE(val)) return ValueType::Type;

    if (SRCLANG_VALUE_IS_OBJECT(val))
        return (SRCLANG_VALUE_AS_OBJECT(val)->type);
    throw runtime_error("invalid value '" + to_string((uint64_t)val) + "'");
}

string srclang::SRCLANG_VALUE_GET_STRING(Value val) {
    auto type = SRCLANG_VALUE_GET_TYPE(val);
    switch (type) {
        case ValueType::Null:
            return "null";
        case ValueType::Boolean:
            return SRCLANG_VALUE_AS_BOOL(val) ? "true" : "false";
        case ValueType::Number: {
            auto value = to_string(SRCLANG_VALUE_AS_NUMBER(val));
            auto idx = value.find_last_not_of('0');
            value = value.substr(0, idx + 1);
            if (value.back() == '.') value.pop_back();
            return value;
        }

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
                        return (char *)object->pointer;
                    case ValueType::List: {
                        stringstream ss;
                        ss << "[";
                        string sep;
                        for (auto const &i : *(reinterpret_cast<SrcLangList *>(object->pointer))) {
                            ss << sep << SRCLANG_VALUE_GET_STRING(i);
                            sep = ", ";
                        }
                        ss << "]";
                        return ss.str();
                    } break;

                    case ValueType::Map: {
                        stringstream ss;
                        ss << "{";
                        string sep;
                        for (auto const &i : *(reinterpret_cast<SrcLangMap *>(object->pointer))) {
                            ss << sep << i.first << ":" << SRCLANG_VALUE_GET_STRING(i.second);
                            sep = ", ";
                        }
                        ss << "}";
                        return ss.str();
                    } break;

                    case ValueType::Function: {
                        return "<function()>";
                    } break;

                    case ValueType::Pointer: {
                        stringstream ss;
                        ss << "0x" << std::hex << object->pointer;
                        return ss.str();
                    }

                    default:
                        return "<object(" + SRCLANG_VALUE_TYPE_ID[int(type)] + ")>";
                }
            }
    }

    return "<value(" + SRCLANG_VALUE_TYPE_ID[int(type)] + ")>";
}
