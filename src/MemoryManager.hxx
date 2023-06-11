
#ifndef SRCLANG_MEMORYMANAGER_HXX
#define SRCLANG_MEMORYMANAGER_HXX

#include "Value.hxx"

namespace srclang {

    struct HeapObject {
        ValueType type{};
        void *pointer{nullptr};

        bool marked{false};
    };

    class MemoryManager {
    private:


    public:
        using Heap = std::vector<Value>;

        Heap heap;

        MemoryManager() = default;

        ~MemoryManager() = default;

        void mark(Value val);

        void mark(Heap::iterator start, Heap::iterator end);

        void sweep();
    };

} // srclang

#endif //SRCLANG_MEMORYMANAGER_HXX
