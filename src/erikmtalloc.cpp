#include <bits/stdc++.h>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <sys/mman.h>
#include <type_traits>
#include <unistd.h>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "erikmtalloc.h"

using namespace std;

#define DEFAULT_CHUNK_SIZE 1024*256 // 256KB chunks

// What are we storing?
// mmap segment total size
// remaining size
// address and size of each allocation

struct allocation {
    allocation *next;
    size_t size;
    bool is_allocated;
    bool is_footer;
    bool is_parent;
    int total_allocations = 0;
};

typedef struct allocation allocation;

char *debug_environ = std::getenv("ERIKMTALLOC_DEBUG");

// Linked list to track allocations for re-use/cleanup
allocation *root;
allocation *cur;
size_t pagesize = sysconf(_SC_PAGE_SIZE);

template <typename ...Args>
void debug(Args&& ...args) {
    std::ostringstream stream;
    (stream << ... << forward<Args>(args));
    //(stream << ... << std::forward<Args>(args)) << '\n';

    cout << stream.str() << endl;
}

void tag_chunk(char *chunk, size_t aligned_size) {
    // Add a header and footer identifying metadata for new chunk,
    // then add the chunk to the chunk map list.
    // Add footer to end of allocation
    struct allocation *footer = (struct allocation*) (static_cast<char*>(chunk) + (aligned_size - sizeof(allocation)));
    cout << "writing CHUNK footer at " << footer << endl;
    
    footer->next = nullptr;
    footer->size = 0;
    footer->is_allocated = false;
    footer->is_footer = true;
    footer->is_parent = true;

    struct allocation *header = (struct allocation*) chunk;
    cout << "writing CHUNK header at " << header << endl;

    header->next = footer;
    header->size = aligned_size - pagesize;
    header->is_allocated = false;
    header->is_footer = false;
    header->is_parent = true;

    if (!cur) {
        // This is the first segment allocated
        root = header;
        cur = header;
    } else {
        allocation* a = cur;
        while (a->next) {
            a = a->next;
        }
        a->next = header;
    }
}

size_t add_chunk(size_t size) {
    // Add a chunk capable of containing at least the size passed.  By default, create
    // a large chunk (specified by DEFAULT_CHUNK_SIZE), unless new() requires more memory
    // than the chunk size, in which case, create a chunk aligned up to the nearest page
    // beyond the requested size.
    size_t aligned_size;

    // Pad to ensure there's enough room for desired allocation + headers/footers structs
    size_t padded_size = (sizeof(allocation)*4) + ((size > DEFAULT_CHUNK_SIZE) ? size : DEFAULT_CHUNK_SIZE);
    aligned_size = (padded_size + (pagesize - (padded_size % pagesize)));

    cout << "Created chunk with size: " << aligned_size << " bytes" << endl;

    char *ptr = static_cast<char*>(mmap(NULL, aligned_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));

    tag_chunk(ptr, aligned_size);

    return aligned_size;
}

void* create_segment_in_chunk(allocation* chunk, size_t size) {
    // Find free space in parent chunk, and reserve it
    cout << "SIZE REMAINING: " << chunk->size << endl;
    allocation* cur = chunk;

    // Update total remaining contiguous space removing allocation size + header/footer padding
    chunk->size = chunk->size - size - (sizeof(allocation)*2) - 1024;
    chunk->total_allocations += 1;

    // Skip to free space
    // Find the pointer to the last segment before the free space begins
    while (cur->next) {
        if (cur->next->is_footer && cur->next->is_parent) break;
        cur = cur->next;
    }

    // Now write a header and footer for the new segment
    // Add footer to end of allocation
    cout << "writing segment footer at " << cur + (sizeof(allocation)*2) + (size/sizeof(allocation)) << endl;
    struct allocation *footer = (struct allocation*) cur + (sizeof(allocation)*2) + (size/sizeof(allocation));

    footer->next = cur->next;
    footer->size = size;
    footer->is_allocated = true;
    footer->is_footer = true;
    footer->is_parent = false;

    cout << "writing segment header at " <<  cur + (sizeof(allocation)) << endl;
    struct allocation *header = (struct allocation*) cur + (sizeof(allocation));

    header->next = footer;
    header->size = size;
    header->is_allocated = true;
    header->is_footer = false;
    header->is_parent = false;

    cur->next = header;

    cout << "Header next is " << header->next << " and footer next is " << footer->next << endl;

    //print_memory_stack();

    return (void*) (cur + sizeof(allocation) + 1);
}

void *get_segment(size_t size) {
    // Find and return a void* pointer to a new memory segment to new()
    void *seg = find_segment(size);

    if (seg == nullptr) {
        // Unable to find space to allocate a new segment, so add a new memory chunk
        add_chunk(size);
    } else {
        return seg;
    }

    return find_segment(size);
}

void *find_segment(size_t minimum_size) {
    //print_memory_stack();

    cout << "Searching for free segment.." << endl;
    allocation *r = root;
    
    while (r != NULL) {
        cout << "Checking " << r << " with size " << r->size << " and allocated state " << (r->is_allocated ? "true" : "false") << endl;
        if (r->size >= (minimum_size+pagesize) && r->is_allocated == false && r->is_parent == true && r->is_footer == false) {
            cout << "Found a free MMAP chunk (need " << minimum_size << " bytes, have " << r->size << " bytes available." << endl;
            return create_segment_in_chunk(r, minimum_size);
        }
        r = r->next;
    }
    cout << "No suitable segments available, need to allocate one." << endl;

    return NULL;
}

void unlink_node(allocation* a, allocation* node_to_remove) {
    allocation* tmp = a;
    if (tmp == node_to_remove) {
        while (tmp->next != nullptr) {
            if (tmp->next->is_parent && tmp->next->is_footer && tmp->next->next == nullptr) {
                // This is the only node
                root = nullptr;
                cur = nullptr;
            }
            tmp = tmp->next;
        }
    } else {
        while (tmp->next != nullptr) {
            if (tmp->next == node_to_remove) {
               allocation *tmpfooter = tmp;
               // Now that we have the footer pointing to the chunk to remove, we need to
               // re-point the footer that points to it to the next chunk header in the list
               tmp = tmp->next;

               while (tmp->next != nullptr) {
                  if (tmp->next->is_parent && !(tmp->next->is_footer)) {
                     // Found the next parent header
                     tmpfooter->next = tmp->next;
                     return;
                  }
                  // If we made it here, this was the last node in the list
                  tmpfooter->next = nullptr;
                  return;
               }
            }
            tmp = tmp->next;
        }
    }
}

void free_segment(void* ptr) {
    cout << "In free_segment() for ptr: " << ptr
        << " Struct ptr is: " << ((void*) (static_cast<char*>(ptr) - sizeof(allocation)))
        << endl;

    void *p = ((void *)(static_cast<char *>(ptr) - sizeof(allocation)));

    allocation *r = root;
    allocation *current_parent_node;

    while (r && r->next != NULL) {
        if (r->is_parent) current_parent_node = r;

        if (r == p) {
            cout << "FOUND SEGMENT TO FREE AT " << r->size << endl;
            r->is_allocated = false;
            current_parent_node->total_allocations -= 1;

            if (current_parent_node->total_allocations == 0) {
                cout << "NO REMAINING ALLOCATIONS, munmap()ing space" << endl;
                unlink_node(root, current_parent_node);
                munmap(current_parent_node, current_parent_node->size);
            }
            return;
        }
        r = r->next;
    }
}

void print_memory_stack() {
    cout << "" << endl;
    cout << "MEMORY ALLOCATOR STACK" << endl;
    cout << "-------------------------------------" << endl;

    allocation *r = root;

    if (r) cout << r << endl;

    while (r != NULL) {
        cout << "SEGMENT: " << r
            << " SIZE: " << r->size
            << " IS_ALLOCATED: " << r->is_allocated
            << " TOTAL_ALLOCATIONS: " << r->total_allocations
            << " IS_PARENT: " << r->is_parent
            << " IS FOOTER: " << r->is_footer
            << " NEXT: " << r->next
            << endl;
        r = r->next;
    }
    cout << "" << endl;
}