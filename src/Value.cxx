#include "Value.hxx"

#include "Function.hxx"
#include "MemoryManager.hxx"
#include "Utilities.hxx"

using namespace srclang;


void srclang::SRCLANG_VALUE_FREE(Value value) {
    if (!SRCLANG_VALUE_IS_OBJECT(value)) {
        return;
    }
    auto type = SRCLANG_VALUE_GET_TYPE(value);
    auto object = SRCLANG_VALUE_AS_OBJECT(value);
    if (!object->is_ref) {
        switch (type) {
            case ValueType::String:
            case ValueType::Error:
                free((char *)object->pointer);
                break;

            case ValueType::List:
                delete reinterpret_cast<SrcLangList *>(object->pointer);
                break;

            case ValueType::Map:
                delete reinterpret_cast<SrcLangMap *>(object->pointer);
                break;

            case ValueType::Function:
                delete reinterpret_cast<Function *>(object->pointer);
                break;

            case ValueType::Closure:
                delete reinterpret_cast<Closure *>(object->pointer);
                break;

            case ValueType::Pointer:
                object->cleanup(object->pointer);
                break;

            case ValueType::Builtin:
                break;

            default:
                throw std::runtime_error("can't clean value of type '" + SRCLANG_VALUE_TYPE_ID[int(type)] + "'");
        }
    }

    delete object;
}
