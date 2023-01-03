void print_memory_stack();
void* get_segment(size_t size);
void* add_segment(size_t size);
void* find_segment(size_t minimum_size);
void free_segment(void* ptr);