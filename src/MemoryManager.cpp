#include "MemoryManager.h"

#include "Function.h"

using namespace SrcLang;

MemoryManager::~MemoryManager() {}

void MemoryManager::unmark() {
    for (auto i : heap) {
        if (SRCLANG_VALUE_IS_OBJECT(i))
            SRCLANG_VALUE_AS_OBJECT(i)->marked = false;
    }
}

void MemoryManager::mark(Value val) {
    if (SRCLANG_VALUE_IS_OBJECT(val)) {
        auto obj = SRCLANG_VALUE_AS_OBJECT(val);
        if (obj->marked) return;
        obj->marked = true;
#ifdef SRCLANG_GC_DEBUG
        std::wcout << L"  marked " << uintptr_t(SRCLANG_VALUE_AS_OBJECT(val)->pointer)
                   << L"'" << SRCLANG_VALUE_GET_STRING(val) << L"'"
                   << std::endl;
#endif
        if (obj->type == ValueType::List) {
            mark(SRCLANG_VALUE_AS_LIST(val)->begin(),
                    SRCLANG_VALUE_AS_LIST(val)->end());
        } else if (obj->type == ValueType::Closure) {
            mark(SRCLANG_VALUE_AS_CLOSURE(val)->free.begin(),
                    SRCLANG_VALUE_AS_CLOSURE(val)->free.end());
        } else if (obj->type == ValueType::Map) {
            for (auto& i : *SRCLANG_VALUE_AS_MAP(val)) { mark(i.second); }
        }
    }
}

void MemoryManager::mark(Heap::iterator start, Heap::iterator end) {
    for (auto i = start; i != end; i++) {
        if (SRCLANG_VALUE_IS_OBJECT(*i) &&
                !SRCLANG_VALUE_AS_OBJECT(*i)->marked) {
            mark(*i);
        }
    }
}

void MemoryManager::sweep() {
    for (auto iter = heap.begin(); iter != heap.end();) {
        if (!SRCLANG_VALUE_IS_OBJECT(*iter)) {
            iter++;
            continue;
        };
        auto object = SRCLANG_VALUE_AS_OBJECT(*iter);
        if (object->marked) {
            object->marked = false;
            iter++;
        } else {
#ifdef SRCLANG_GC_DEBUG
            std::wcout << L"   deallocating " << uintptr_t(object->pointer) << L"'"
                       << SRCLANG_VALUE_GET_STRING(*iter) << L"'" << std::endl;
#endif
            SRCLANG_VALUE_FREE(*iter);
            iter = heap.erase(iter);
        }
    }
}

Value MemoryManager::addObject(Value value) {
    if (SRCLANG_VALUE_IS_OBJECT(value)) heap.emplace_back(value);
    return value;
}
