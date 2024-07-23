#include "Value.h"

#include "Function.h"
#include "MemoryManager.h"
#include <sstream>

using namespace SrcLang;

const char* Native::TYPE_TO_STR[int(Native::Type::N_Type)] = {
#define X(id) #id,
        NATIVE_TYPE_LIST
#undef X
};

Native::Type Native::fromString(const char* s) {
#define X(id)                                                                  \
    if (strcmp(s, #id) == 0) return Native::Type::id;
    NATIVE_TYPE_LIST
#undef X
    throw std::runtime_error("invalid ctype");
}

void SrcLang::SRCLANG_VALUE_FREE(Value value) {
    if (!SRCLANG_VALUE_IS_OBJECT(value)) { return; }
    auto object = SRCLANG_VALUE_AS_OBJECT(value);
    if (!object->is_ref) {
        switch (object->type) {
        case ValueType::String:
        case ValueType::Error: free(SRCLANG_VALUE_AS_STRING(value)); break;

        case ValueType::List:
            delete reinterpret_cast<SrcLangList*>(object->pointer);
            break;

        case ValueType::Map:
            delete reinterpret_cast<SrcLangMap*>(object->pointer);
            break;

        case ValueType::Function:
            delete reinterpret_cast<Function*>(object->pointer);
            break;

        case ValueType::Closure:
            delete reinterpret_cast<Closure*>(object->pointer);
            break;

        case ValueType::Bounded:
            delete reinterpret_cast<BoundedValue*>(object->pointer);
            break;

        case ValueType::Pointer: object->cleanup(object->pointer); break;

        case ValueType::Native:
            delete reinterpret_cast<Native*>(object->pointer);
            break;

        case ValueType::Builtin: break;

        default:
            throw std::runtime_error("can't clean value of type '" +
                                     SRCLANG_VALUE_TYPE_ID[int(object->type)] +
                                     "'");
        }
    }

    delete object;
}

ValueType SrcLang::SRCLANG_VALUE_GET_TYPE(Value val) {
    if (SRCLANG_VALUE_IS_NULL(val)) return ValueType::Null;
    if (SRCLANG_VALUE_IS_BOOL(val)) return ValueType::Boolean;
    if (SRCLANG_VALUE_IS_NUMBER(val)) return ValueType::Number;
    if (SRCLANG_VALUE_IS_TYPE(val)) return ValueType::Type;

    if (SRCLANG_VALUE_IS_OBJECT(val))
        return (SRCLANG_VALUE_AS_OBJECT(val)->type);
    throw std::runtime_error(
            "invalid value '" + std::to_string((uint64_t)val) + "'");
}

std::wstring SrcLang::SRCLANG_VALUE_GET_STRING(Value val) {
    auto type = SRCLANG_VALUE_GET_TYPE(val);
    switch (type) {
    case ValueType::Null: return L"null";
    case ValueType::Boolean:
        return SRCLANG_VALUE_AS_BOOL(val) ? L"true" : L"false";
    case ValueType::Number: {
        auto value = std::to_wstring(SRCLANG_VALUE_AS_NUMBER(val));
        auto idx = value.find_last_not_of('0');
        value = value.substr(0, idx + 1);
        if (value.back() == '.') value.pop_back();
        return value;
    }

    case ValueType::Type:
        return L"<type(" +
               s2ws(SRCLANG_VALUE_TYPE_ID[int(SRCLANG_VALUE_AS_TYPE(val))]) +
               L")>";
    default:
        if (SRCLANG_VALUE_IS_OBJECT(val)) {
            auto object = SRCLANG_VALUE_AS_OBJECT(val);
            switch (type) {
            case ValueType::String:
            case ValueType::Error:
                return static_cast<wchar_t*>(object->pointer);
            case ValueType::List: {
                std::wstringstream ss;
                ss << L"[";
                std::wstring sep;
                for (auto const& i :
                        *(static_cast<SrcLangList*>(object->pointer))) {
                    ss << sep << SRCLANG_VALUE_GET_STRING(i);
                    sep = L", ";
                }
                ss << L"]";
                return ss.str();
            } break;

            case ValueType::Map: {
                std::wstringstream ss;
                ss << L"{";
                std::wstring sep;
                for (auto const& i :
                        *(reinterpret_cast<SrcLangMap*>(object->pointer))) {
                    ss << sep << i.first << ":"
                       << SRCLANG_VALUE_GET_STRING(i.second);
                    sep = L", ";
                }
                ss << L"}";
                return ss.str();
            } break;

            case ValueType::Function: {
                return L"<function(" + SRCLANG_VALUE_AS_FUNCTION(val)->id +
                       L")>";
            } break;

            case ValueType::Closure: {
                return L"<closure(" + SRCLANG_VALUE_AS_CLOSURE(val)->fun->id +
                       L")>";
            } break;

            case ValueType::Native: {
                return L"<native(" + SRCLANG_VALUE_AS_NATIVE(val)->id + L")>";
            } break;

            case ValueType::Pointer: {
                std::wstringstream ss;
                ss << std::hex << object->pointer;
                return ss.str();
            }

            default:
                return L"<object(" + s2ws(SRCLANG_VALUE_TYPE_ID[int(type)]) +
                       L")>";
            }
        }
    }

    return L"<value(" + s2ws(SRCLANG_VALUE_TYPE_ID[int(type)]) + L")>";
}
