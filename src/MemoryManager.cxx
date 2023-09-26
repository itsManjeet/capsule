#include "MemoryManager.hxx"

#include "Function.hxx"

using namespace srclang;

MemoryManager::~MemoryManager() {
    sweep();
    sweep();
}

void MemoryManager::mark(Value val) {
    if (SRCLANG_VALUE_IS_OBJECT(val)) {
        auto obj = SRCLANG_VALUE_AS_OBJECT(val);
        if (obj->marked) return;
        obj->marked = true;
#ifdef SRCLANG_GC_DEBUG
        std::cout << "  marked "
                  << uintptr_t(SRCLANG_VALUE_AS_OBJECT(val)->pointer) << "'"
                  << SRCLANG_VALUE_GET_STRING(val) << "'" << std::endl;
#endif
        if (obj->type == ValueType::List) {
            mark(reinterpret_cast<std::vector<Value> *>(obj->pointer)->begin(),
                 reinterpret_cast<std::vector<Value> *>(obj->pointer)->end());
        } else if (obj->type == ValueType::Closure) {
            mark(reinterpret_cast<Closure *>(obj->pointer)->free.begin(),
                 reinterpret_cast<Closure *>(obj->pointer)->free.end());
        } else if (obj->type == ValueType::Map) {
            for (auto &i : *reinterpret_cast<SrcLangMap *>(obj->pointer)) {
                mark(i.second);
            }
        }
    }
}

void MemoryManager::mark(Heap::iterator start, Heap::iterator end) {
    for (auto i = start; i != end; i++) {
        mark(*i);
    }
}

void MemoryManager::sweep() {
    for (auto i = heap.begin(); i != heap.end();) {
        if (SRCLANG_VALUE_IS_OBJECT(*i)) {
            auto obj = SRCLANG_VALUE_AS_OBJECT(*i);
            if (obj->marked) {
                obj->marked = false;
                i++;
            } else {
#ifdef SRCLANG_GC_DEBUG
                std::cout << "   deallocating "
                          << uintptr_t(obj->pointer) << "'"
                          << SRCLANG_VALUE_GET_STRING(*i)
                          << "'" << std::endl;
#endif
                SRCLANG_VALUE_FREE(*i);
                i = heap.erase(i);
            }
        } else {
            i++;
        }
    }
}