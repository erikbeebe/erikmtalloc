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

// Structure for segments allocated inside of an MMAPed "chunk"
struct segment_s {
    segment_s* next;
    size_t size;
    bool is_allocated;
    bool is_footer;
};

// mmap()'ed parent "chunks", on which variable size "segments" are allocated
struct chunk_s {
    chunk_s* next;
    size_t allocated_size;
    size_t remaining_size;
    bool is_allocated;
    bool is_footer;
    bool is_parent;
    int total_allocations = 0;
    segment_s* next_segment;
};

typedef struct chunk_s chunk_s;
typedef struct segment_s segment_s;

// Linked list to track allocations for re-use/cleanup
chunk_s* root;
chunk_s* cur;
size_t pagesize = sysconf(_SC_PAGE_SIZE);

// Add a header and footer identifying metadata for new chunk,
// then add the chunk to the chunk map list.
// Add footer to end of allocation
void tag_chunk(char *chunk, size_t aligned_size) {
    struct chunk_s* footer = reinterpret_cast<chunk_s*>(static_cast<char*>(chunk) + (aligned_size - sizeof(chunk_s)));
    debug(std::cout, "writing CHUNK footer at", footer);
    
    footer->next = nullptr;
    // Size tracked in header only
    footer->allocated_size = 0;
    footer->remaining_size = 0;
    footer->is_allocated = false;
    footer->is_footer = true;
    footer->is_parent = true;
    footer->next_segment = nullptr;

    struct chunk_s* header = reinterpret_cast<chunk_s*>((static_cast<char*>(chunk)));
    debug(std::cout, "writing CHUNK header at", header);

    header->next = footer;
    header->allocated_size = aligned_size - pagesize;
    header->remaining_size = aligned_size - pagesize;
    header->is_allocated = false;
    header->is_footer = false;
    header->is_parent = true;
    header->next_segment = nullptr;

    if (!cur) {
        // This is the first segment allocated
        root = header;
        cur = header;
    } else {
        chunk_s* a = cur;
        while (a->next) {
            a = a->next;
        }
        a->next = header;
    }
}

// Align to OS page size
size_t align_to_pagesize(size_t size) {
    return (size_t) (size + (pagesize - (size % pagesize)));
}

// Align size to next nearest 4 byte boundary
size_t align_4(size_t size) {
    return (size_t) (size + (4 - (size % 4)));
}

// Get a pointer to the first byte of the payload section
// from a segment_s header
void* get_payload(uintptr_t addr) {
    return reinterpret_cast<void *>(align_4(addr + sizeof(segment_s)));
}

// Get a pointer to the header struct of payload
void* get_header(void* ptr) {
    return reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(ptr) - (sizeof(segment_s)));
}

// Add a chunk capable of containing at least the size passed.  By default, create
// a large chunk (specified by DEFAULT_CHUNK_SIZE), unless new() requires more memory
// than the chunk size, in which case, create a chunk aligned up to the nearest page
// beyond the requested size.
size_t add_chunk(size_t size) {
    size_t aligned_size;

    // Pad to ensure there's enough room for desired allocation + headers/footers structs
    size_t padded_size = (sizeof(chunk_s)*4) + ((size > DEFAULT_CHUNK_SIZE) ? size : DEFAULT_CHUNK_SIZE);
    aligned_size = align_to_pagesize(padded_size);

    debug(std::cout, "Created chunk with size:", aligned_size, "bytes");

    char *ptr = static_cast<char*>(mmap(NULL, aligned_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));

    tag_chunk(ptr, aligned_size);

    return aligned_size;
}

// Find free space in parent chunk, and reserve it
void* create_segment_in_chunk(chunk_s* chunk, size_t size) {
    debug(std::cout, "SIZE REMAINING:", chunk->remaining_size);
    segment_s* segment_iter = chunk->next_segment;

    // Update total remaining contiguous space removing allocation size + header/footer padding
    // TODO: Align this?
    chunk->remaining_size = chunk->remaining_size - (size + (sizeof(segment_s)*2));
    chunk->total_allocations += 1;

    // Skip to free space pool
    // Find the pointer to the last segment before the free space begins
    uintptr_t free_space_ptr;
    if (segment_iter == nullptr) {
        // No segments currently exist in this chunk, initialize with offset from chunk_s header
        free_space_ptr = reinterpret_cast<uintptr_t>(chunk) + sizeof(chunk_s) + 1;
    } else {
        // Iterate to end of currently allocated space
        while (segment_iter->next) {
            segment_iter = segment_iter->next;
        }
        free_space_ptr = reinterpret_cast<uintptr_t>(segment_iter) + sizeof(segment_s) + 1;
    }

    // Now write a header and footer for the new segment
    // Add footer to end of allocation
    struct segment_s* footer = reinterpret_cast<segment_s*>(align_4(free_space_ptr + sizeof(segment_s) + size));

    footer->next = nullptr;
    footer->size = size;
    footer->is_allocated = true;
    footer->is_footer = true;

    struct segment_s* header = reinterpret_cast<segment_s*>(align_4(free_space_ptr));

    header->next = footer;
    header->size = size;
    header->is_allocated = true;
    header->is_footer = false;

    if (chunk->next_segment == nullptr) {
        chunk->next_segment = header;
    } else {
        segment_s* header_iter = chunk->next_segment;
        while (header_iter->next) {
            header_iter = header_iter->next;
        }
        header_iter->next = header;
    }

    debug(std::cout, "Header next is", header->next, "and footer next is", footer->next);

#ifdef DEBUG
    print_memory_stack();
#endif

    debug(std::cout, "Returning ptr to", reinterpret_cast<void *>(align_4(free_space_ptr + sizeof(segment_s))));
    return get_payload(free_space_ptr);
}

// Find free space in parent chunk (if it exists), reserve it,
// and return a void* pointer to it.
void* reserve_segment(chunk_s* parent_chunk, segment_s* segment, size_t size) {
    debug(std::cout, "Reusing segment:", segment, "in parent chunk:", parent_chunk,
      "with size:", segment->size, "for new segment of size:", size);

    segment->size = size;
    segment->is_allocated = true;

    parent_chunk->total_allocations += 1;
#ifdef DEBUG
    print_memory_stack();
#endif

    debug(std::cout, "Returning reuse ptr to space at", reinterpret_cast<void *>((uintptr_t) segment  + sizeof(segment_s)));
    // Already aligned
    return reinterpret_cast<void *>((uintptr_t) segment + sizeof(segment_s));
}

// Find and return a void* pointer to a new memory segment to new()
// The second call to find_segment() is guaranteed to return a segment if
// add_chunk succeeds (as this is happening in a mutex that prevents anyone)
// else from claiming it.  There's a little unnecessary overhead since we
// walk the linked list from root to get there, so it makes sense to refactor
// this later to have add_chunk also create the first segment and return the
// pointer to it directly.
void *get_segment(size_t size) {
    void *seg = find_segment(size);

    if (seg == nullptr) {
        // Unable to find space to allocate a new segment, so add a new memory chunk
        add_chunk(size);
    } else {
        return seg;
    }

    return find_segment(size);
}

// Locate (or create) and return a viable segment, and return a void* to it.
void *find_segment(size_t minimum_size) {
#ifdef DEBUG
    print_memory_stack();
#endif

    debug(std::cout, "Searching for free segment..");

    chunk_s* r = root;
    chunk_s* parent_chunk;
    
    while (r) {
        debug(std::cout, "Checking CHUNK", r, "with size", r->allocated_size, "and remaining space", r->remaining_size);
        parent_chunk = r;
        segment_s* segment_iter = r->next_segment;

        while (segment_iter) {
            // Look for a free segment to reclaim while searching for free chunk space
            // If we find it, re-use instead of allocating new segments
            if (segment_iter->size >= (minimum_size) && segment_iter->is_allocated == false && segment_iter->is_footer == false) {
                // Located an existing segment large enough for allocation and marked unallocated
                debug(std::cout, "Found a reusable segment in chunk (need", minimum_size, "bytes, have", segment_iter->size, "bytes available.");
                return reserve_segment(parent_chunk, segment_iter, minimum_size);
            }
            segment_iter = segment_iter->next;
        }

        if (r->remaining_size >= (minimum_size+pagesize) && r->is_footer == false) {
            // Unable to find an unallocated segment, allocate a new segment in chunk
            debug(std::cout, "Found a free MMAP chunk (need", minimum_size, "bytes, have", r->remaining_size, "bytes available.");
            return create_segment_in_chunk(r, minimum_size);
        } else {
            debug(std::cout, "Skipping chunk", r, "- no space available");
        }

        r = r->next;
    }

    debug(std::cout, "No suitable segments available, need to allocate one.");

    return nullptr;
}

void unlink_node(chunk_s* root_node, chunk_s* node_to_remove) {
    chunk_s* current_node = root_node;
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
               chunk_s* current_footer = current_node;
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
    void* parent_struct = get_header(ptr);

    debug(std::cout, "In free_segment() for ptr: ", ptr, "struct ptr is: ", parent_struct);

    chunk_s* r = root;
    chunk_s* current_parent_node;

    while (r) {
        // Check that pointer address that we're freeing is in the range of allocated
        // space for this chunk.  If it's not, don't bother descending into this chunks
        // segments, move to next chunk.
        if (!(r < ptr && (r + r->allocated_size) > ptr)) {
            debug(std::cout, "Skipping chunk", r, "in free for ptr address", ptr);
            r = r->next;
            continue;
        }

        current_parent_node = r;
        segment_s* segment_iter = r->next_segment;

        while (segment_iter) {
            if (segment_iter == parent_struct) {
                debug(std::cout, "Located segment to free at", segment_iter->size);
                segment_iter->is_allocated = false;
                current_parent_node->total_allocations -= 1;

                if (current_parent_node->total_allocations == 0) {
                    debug(std::cout, "No remaining allocated segments in chunk, munmap()ing chunk space");
                    unlink_node(root, current_parent_node);
                    munmap(current_parent_node, current_parent_node->allocated_size);
                }
                return;
            }
            segment_iter = segment_iter->next;
        }

        r = r->next;
    }
}

void print_memory_stack() {
    std::cout << "" << std::endl;
    std::cout << "MEMORY ALLOCATOR STACK" << std::endl;
    std::cout << "-------------------------------------" << std::endl;

    chunk_s* r = root;

    if (r) std::cout << r << std::endl;

    while (r) {
        std::cout << std::boolalpha
            << "SEGMENT: " << r
            << " ALLOCATED SIZE: " << r->allocated_size
            << " REMAINING SIZE: " << r->remaining_size
            << " IS_ALLOCATED: " << r->is_allocated
            << " TOTAL_ALLOCATIONS: " << r->total_allocations
            << " IS_PARENT: " << r->is_parent
            << " IS FOOTER: " << r->is_footer
            << " NEXT: " << r->next
            << std::endl;
        
        segment_s* segment_iter = r->next_segment;

        while (segment_iter) {
            std::cout << std::boolalpha
                << "SEGMENT: " << segment_iter
                << " SIZE: " << segment_iter->size
                << " IS_ALLOCATED: " << segment_iter->is_allocated
                << " IS FOOTER: " << segment_iter->is_footer
                << " NEXT: " << segment_iter->next
                << std::endl;

            segment_iter = segment_iter->next;
        }

        r = r->next;
    }
    std::cout << "-------------------------------------------------------" << std::endl;
    std::cout << "" << std::endl;
}