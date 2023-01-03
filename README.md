# Experimental C++ memory allocator

Goals:

- Override new/delete
- Thread safe
- Variable size allocations on mmap()ed chunk space

Developed/tested on gcc/g++ version 9.4.0
