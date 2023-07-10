#include "Value.hxx"
#include "Utilities.hxx"
#include "MemoryManager.hxx"
#include "Function.hxx"

using namespace srclang;

void srclang::SRCLANG_VALUE_DUMP(Value v, std::ostream &os) {
    auto valueType = SRCLANG_VALUE_GET_TYPE(v);
    os.write(reinterpret_cast<const char *>(&valueType), sizeof(ValueType));
    if (!SRCLANG_VALUE_IS_OBJECT(v)) {
        os.write(reinterpret_cast<const char *>(&v), sizeof(Value));
        return;
    }

    auto object = SRCLANG_VALUE_AS_OBJECT(v);
    object->type = valueType;
    switch (object->type) {
        case ValueType::String:
        case ValueType::Error:
            dump_string((const char *) object->pointer, os);
            break;
        case ValueType::List: {
            auto list = (SrcLangList *) object->pointer;
            dump_int<size_t>(list->size(), os);
            for (auto &i: *list) {
                SRCLANG_VALUE_DUMP(i, os);
            }
            break;
        }

        case ValueType::Map: {
            auto map_ = (SrcLangMap *) object->pointer;
            dump_int<size_t>(map_->size(), os);
            for (auto &i: *map_) {
                dump_string(i.first, os);
                SRCLANG_VALUE_DUMP(i.second, os);
            }
            break;
        }

        case ValueType::Function: {
            auto function = (Function *) object->pointer;
            os.write(reinterpret_cast<const char *>(&function->type), sizeof(function->type));
            dump_string(function->id, os);
            dump_int<size_t>(function->instructions->size(), os);
            for (auto i: *function->instructions) {
                os.write(reinterpret_cast<const char *>(&i), sizeof(i));
            }
            dump_int<int>(function->nlocals, os);
            dump_int<int>(function->nparams, os);
            dump_int<bool>(function->is_variadic, os);
            function->debug_info->dump(os);
        }
            break;

        case ValueType::Native: {
            auto native = (NativeFunction *) object->pointer;
            dump_int<ValueType>(native->ret, os);
            dump_int<size_t>(native->param.size(), os);
            for (auto i: native->param) {
                dump_int<CType>(i, os);
            }
            dump_string(native->id, os);
        }
            break;

        default:
            throw std::runtime_error("can't dump value '" + SRCLANG_VALUE_TYPE_ID[int(object->type)] + "'");
    }
}

Value srclang::SRCLANG_VALUE_READ(std::istream &is) {
    ValueType valueType;
    is.read(reinterpret_cast<char *>(&valueType), sizeof(ValueType));
    if (valueType <= ValueType::Char) {
        Value value;
        is.read(reinterpret_cast<char *>(&value), sizeof(Value));
        return value;
    }
    auto object = new HeapObject();
    object->type = valueType;
    switch (object->type) {
        case ValueType::String:
        case ValueType::Error:
            object->pointer = (void *) strdup(read_string(is).c_str());
            break;
        case ValueType::List: {
            auto list = new SrcLangList();
            auto size = read_int<size_t>(is);
            for (auto i = 0; i < size; i++) {
                list->push_back(SRCLANG_VALUE_READ(is));
            }
            object->pointer = (void *) list;
            break;
        }

        case ValueType::Map: {
            auto map_ = new SrcLangMap();
            auto size = read_int<size_t>(is);
            for (auto i = 0; i < size; i++) {
                std::string k = read_string(is);
                Value v = SRCLANG_VALUE_READ(is);
                map_->insert({k, v});
            }
            object->pointer = (void *) map_;
            break;
        }

        case ValueType::Function: {
            auto function = new Function();
            function->instructions = std::make_unique<Instructions>();
            is.read(reinterpret_cast<char *>(&function->type), sizeof(function->type));
            function->id = read_string(is);
            auto size = read_int<size_t>(is);
            for (auto i = 0; i < size; i++) {
                Byte byte;
                is.read(reinterpret_cast<char *>(&byte), sizeof(Byte));
                function->instructions->push_back(byte);
            }
            function->nlocals = read_int<int>(is);
            function->nparams = read_int<int>(is);
            function->is_variadic = read_int<bool>(is);
            function->debug_info = std::shared_ptr<DebugInfo>(DebugInfo::read(is));
            object->pointer = (void *) function;
        }
            break;

        case ValueType::Native: {
            auto native = new NativeFunction();
            native->ret = read_int<ValueType>(is);
            auto size = read_int<size_t>(is);
            for (int i = 0; i < size; i++) {
                native->param.push_back(read_int<CType>(is));
            }
            native->id = read_string(is);
            object->pointer = (void *) native;
        }
            break;

        default:
            throw std::runtime_error("can't dump value '" + SRCLANG_VALUE_TYPE_ID[int(object->type)] + "'");
    }

    return SRCLANG_VALUE_OBJECT(object);
}


void srclang::SRCLANG_VALUE_FREE(Value value) {
    if (!SRCLANG_VALUE_IS_OBJECT(value)) {
        return;
    }
    auto type = SRCLANG_VALUE_GET_TYPE(value);
    auto object = SRCLANG_VALUE_AS_OBJECT(value);
    switch (type) {
        case ValueType::String:
        case ValueType::Error:
            free((char *) object->pointer);
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

        case ValueType::Pointer:
            break;

        default:
            throw std::runtime_error("can't clean value of type '" + SRCLANG_VALUE_TYPE_ID[int(type)] + "'");
    }
    delete object;
}
