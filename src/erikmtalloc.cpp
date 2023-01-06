#include <cstdint>
#include <ios>
#include <ostream>
#include <sys/mman.h>
#include <unistd.h>
#include <thread>
#include <mutex>

#include "erikmtalloc.h"
#include "utils.h"

#define DEFAULT_CHUNK_SIZE 1024*256 // 256KB chunks

struct allocation {
    allocation *next;
    size_t size;
    bool is_allocated;
    bool is_footer;
    bool is_parent;
    int total_allocations = 0;
};

typedef struct allocation allocation;

// Linked list to track allocations for re-use/cleanup
allocation *root;
allocation *cur;
size_t pagesize = sysconf(_SC_PAGE_SIZE);

void tag_chunk(char *chunk, size_t aligned_size) {
    // Add a header and footer identifying metadata for new chunk,
    // then add the chunk to the chunk map list.
    // Add footer to end of allocation
    struct allocation *footer = (struct allocation*) (static_cast<char*>(chunk) + (aligned_size - sizeof(allocation)));
    debug(std::cout, "writing CHUNK footer at", footer);
    
    footer->next = nullptr;
    footer->size = 0;
    footer->is_allocated = false;
    footer->is_footer = true;
    footer->is_parent = true;

    struct allocation *header = (struct allocation*) (static_cast<char*>(chunk));
    debug(std::cout, "writing CHUNK header at", header);

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

size_t align_to_pagesize(size_t size) {
    return (size_t) (size + (pagesize - (size % pagesize)));
}

size_t align_4(size_t size) {
    // Align size to next nearest 4 byte boundary
    return (size_t) (size + (4 - (size % 4)));
}

void* get_payload(uintptr_t addr) {
    // Get a pointer to the first byte of the payload section
    return reinterpret_cast<void *>(align_4(addr + sizeof(allocation)));
}

void* get_header(void* ptr) {
    // Get a pointer to the header struct of payload
    return reinterpret_cast<void *>((uintptr_t) ptr - (sizeof(allocation)));
}

size_t add_chunk(size_t size) {
    // Add a chunk capable of containing at least the size passed.  By default, create
    // a large chunk (specified by DEFAULT_CHUNK_SIZE), unless new() requires more memory
    // than the chunk size, in which case, create a chunk aligned up to the nearest page
    // beyond the requested size.
    size_t aligned_size;

    // Pad to ensure there's enough room for desired allocation + headers/footers structs
    size_t padded_size = (sizeof(allocation)*4) + ((size > DEFAULT_CHUNK_SIZE) ? size : DEFAULT_CHUNK_SIZE);
    aligned_size = align_to_pagesize(padded_size);

    debug(std::cout, "Created chunk with size:", aligned_size, "bytes");

    char *ptr = static_cast<char*>(mmap(NULL, aligned_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));

    tag_chunk(ptr, aligned_size);

    return aligned_size;
}

void* create_segment_in_chunk(allocation* chunk, size_t size) {
    // Find free space in parent chunk, and reserve it
    debug(std::cout, "SIZE REMAINING:", chunk->size);
    allocation* chunk_iter = chunk;

    // Update total remaining contiguous space removing allocation size + header/footer padding
    chunk->size = chunk->size - (size + (sizeof(allocation)*2));
    chunk->total_allocations += 1;

    // Skip to free space
    // Find the pointer to the last segment before the free space begins
    while (chunk_iter->next) {
        if (chunk_iter->next->is_footer && chunk_iter->next->is_parent) break;
        chunk_iter = chunk_iter->next;
    }

    uintptr_t free_space_ptr = (uintptr_t) chunk_iter + sizeof(allocation) + 1;

    // Now write a header and footer for the new segment
    // Add footer to end of allocation
    debug(std::cout, "writing segment footer at 0x", std::hex, (align_4(free_space_ptr + size)), std::dec);
    struct allocation *footer = (struct allocation*) reinterpret_cast<void*>(align_4(free_space_ptr + sizeof(allocation) + size + 1));

    footer->next = chunk_iter->next;
    footer->size = size;
    footer->is_allocated = true;
    footer->is_footer = true;
    footer->is_parent = false;

    debug(std::cout, "writing segment header at 0x", std::hex, (align_4(free_space_ptr + sizeof(allocation))), std::dec);
    struct allocation *header = (struct allocation*) reinterpret_cast<void*>(align_4(free_space_ptr));

    header->next = footer;
    header->size = size;
    header->is_allocated = true;
    header->is_footer = false;
    header->is_parent = false;

    chunk_iter->next = header;

    debug(std::cout, "Header next is", header->next, "and footer next is", footer->next);

#ifdef DEBUG
    print_memory_stack();
#endif

    debug(std::cout, "Returning ptr to", reinterpret_cast<void *>(align_4(free_space_ptr + sizeof(allocation))));
    return get_payload(free_space_ptr);
}

void* reserve_segment(allocation* parent_chunk, allocation* segment, size_t size) {
    // Find free space in parent chunk, and reserve it
    debug(std::cout, "Reusing segment:", segment, "in parent chunk:", parent_chunk,
      "with size:", segment->size, "for new segment of size:", size);

    segment->size = size;
    segment->is_allocated = true;

    parent_chunk->total_allocations += 1;
#ifdef DEBUG
    print_memory_stack();
#endif

    debug(std::cout, "Returning reuse ptr to space at", reinterpret_cast<void *>((uintptr_t) segment  + sizeof(allocation)));
    // Already aligned
    return reinterpret_cast<void *>((uintptr_t) segment + sizeof(allocation));
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
#ifdef DEBUG
    print_memory_stack();
#endif

    debug(std::cout, "Searching for free segment..");

    allocation* r = root;
    allocation* parent_chunk;
    
    while (r != NULL) {
        debug(std::cout, "Checking", r, "with size", r->size, "and allocated state", (r->is_allocated ? "true" : "false"));
        if (r->is_parent == true && r->is_footer == false) parent_chunk = r;

        // Opportunistically look for a free segment to reclaim while searching for free chunk space
        // If we find it, re-use instead of allocating new segments
        if (r->size >= (minimum_size) && r->is_allocated == false && r->is_parent == false && r->is_footer == false) {
          // Located an existing segment large enough for allocation and marked unallocated
          debug(std::cout, "Found a reusable segment in chunk (need", minimum_size, "bytes, have", r->size, "bytes available.");
          return reserve_segment(parent_chunk, r, minimum_size);

        } else if (r->size >= (minimum_size+pagesize) && r->is_allocated == false && r->is_parent == true && r->is_footer == false) {
          // Unable to find an unallocated segment, allocate a new segment in chunk
            debug(std::cout, "Found a free MMAP chunk (need", minimum_size, "bytes, have", r->size, "bytes available.");
            return create_segment_in_chunk(r, minimum_size);
        }
        r = r->next;
    }

    debug(std::cout, "No suitable segments available, need to allocate one.");

    return NULL;
}

void unlink_node(allocation* root_node, allocation* node_to_remove) {
    allocation *current_node = root_node;
    if (current_node == node_to_remove) {
        while (current_node->next != nullptr) {
            if (current_node->next->is_parent && current_node->next->is_footer && current_node->next->next == nullptr) {
                // This is the only node
                root = nullptr;
                cur = nullptr;
            }
            current_node = current_node->next;
        }
    } else {
        while (current_node->next != nullptr) {
            if (current_node->next == node_to_remove) {
               allocation *current_footer = current_node;
               // Now that we have the footer pointing to the chunk to remove, we need to
               // re-point the footer that points to it to the next chunk header in the list
               current_node = current_node->next;

               while (current_node->next != nullptr) {
                  if (current_node->next->is_parent && !(current_node->next->is_footer)) {
                     // Found the next parent header
                     current_footer->next = current_node->next;
                     return;
                  }
                  // If we made it here, this was the last node in the list
                  current_footer->next = nullptr;
                  return;
               }
            }
            current_node = current_node->next;
        }
    }
}

void free_segment(void* ptr) {
    void *parent_struct = get_header(ptr);

    debug(std::cout, "In free_segment() for ptr: ", ptr, "struct ptr is: ", parent_struct);

    allocation *r = root;
    allocation *current_parent_node;

    while (r && r->next != NULL) {
        if (r->is_parent) current_parent_node = r;

        if (r == parent_struct) {
            debug(std::cout, "Located segment to free at", r->size);
            r->is_allocated = false;
            current_parent_node->total_allocations -= 1;

            if (current_parent_node->total_allocations == 0) {
                debug(std::cout, "No remaining allocated segments in chunk, munmap()ing chunk space");
                unlink_node(root, current_parent_node);
                munmap(current_parent_node, current_parent_node->size);
            }
            return;
        }
        r = r->next;
    }
}

void print_memory_stack() {
    std::cout << "" << std::endl;
    std::cout << "MEMORY ALLOCATOR STACK" << std::endl;
    std::cout << "-------------------------------------" << std::endl;

    allocation *r = root;

    if (r) std::cout << r << std::endl;

    while (r != NULL) {
        std::cout << std::boolalpha
            << "SEGMENT: " << r
            << " SIZE: " << r->size
            << " IS_ALLOCATED: " << r->is_allocated
            << " TOTAL_ALLOCATIONS: " << r->total_allocations
            << " IS_PARENT: " << r->is_parent
            << " IS FOOTER: " << r->is_footer
            << " NEXT: " << r->next
            << std::endl;
        r = r->next;
    }
    std::cout << "" << std::endl;
}