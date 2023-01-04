#include <iostream>

#define DEBUG 1

template <typename Arg, typename... Args>
void debug(std::ostream& out, Arg&& arg, Args&&... args)
{
#ifndef DEBUG
    return;
#endif

    out << std::forward<Arg>(arg);
    ((out << ' ' << std::forward<Args>(args)), ...);
    out << std::endl;
}
