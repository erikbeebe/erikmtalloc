#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <sys/mman.h>
#include <type_traits>
#include <unistd.h>
#include <thread>
#include <mutex>

#include "erikmtalloc.h"
#include "utils.h"

using namespace std;

mutex mut;

// Check free list to see if we already have a large enough contiguous segment
// If not, mmap more memory, beyond the nearest page boundary, and add to free list
void* operator new(size_t size) {
    debug(std::cout, "NEW: Request for:", size, "bytes");
    debug(std::cout, "Acquiring lock in operator::new");

    unique_lock<mutex> allocation_lock(mut);

    void* ptr = get_segment(size);

    debug(std::cout, "Releasing lock in operator::new");
    allocation_lock.unlock();

    if (!ptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

void operator delete(void* ptr) {
    debug(std::cout, "Acquiring lock in operator::delete");
    unique_lock<mutex> allocation_lock(mut);

    debug(std::cout, "Delete for ptr", ptr);
    free_segment(ptr);

    debug(std::cout, "Releasing lock in operator::delete");
    allocation_lock.unlock();
}