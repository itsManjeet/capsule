
#ifndef SRCLANG_MEMORYMANAGER_HXX
#define SRCLANG_MEMORYMANAGER_HXX

#include "Value.hxx"

namespace srclang {

    struct HeapObject {
        ValueType type{};
        void *pointer{nullptr};
        bool is_ref{false};

        bool marked{false};
    };

    static inline void srclang_value_set_ref(Value value) {
        if (!SRCLANG_VALUE_IS_OBJECT(value)) return;
        SRCLANG_VALUE_AS_OBJECT(value)->is_ref = true;
    }

    class MemoryManager {
       private:
       public:
        using Heap = std::vector<Value>;

        Heap heap;

        MemoryManager() = default;

        ~MemoryManager();

        void mark(Value val);

        void mark(Heap::iterator start, Heap::iterator end);

        void sweep();
    };

}  // srclang

#endif  // SRCLANG_MEMORYMANAGER_HXX
