#include <bits/stdc++.h>
#include <cstddef>
#include <cstdint>
#include <sys/mman.h>
#include <type_traits>
#include <unistd.h>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "erikmtalloc.h"

using namespace std;

mutex mut;

// Check free list to see if we already have a large enough contiguous segment
// If not, mmap more memory, beyond the nearest page boundary, and add to free list
void* operator new(size_t size) {
    cout << "NEW: Request for: " << size << " bytes" << endl;

    cout << "Acquiring lock" << endl;
    unique_lock<mutex> allocation_lock(mut);

    auto ptr = get_segment(size);

    cout << "Releasing lock" << endl;
    allocation_lock.unlock();

    return ptr;
}

void operator delete(void* ptr) {
    cout << "Acquiring lock" << endl;
    unique_lock<mutex> allocation_lock(mut);

    cout << "Delete for ptr " << ptr << endl;
    free_segment(ptr);

    cout << "Releasing lock" << endl;
    allocation_lock.unlock();
}