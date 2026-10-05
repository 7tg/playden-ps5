#include <fcntl.h>
#include <stdarg.h>
#include <sys/types.h>

// The SDK provides openat through its static libc adapter. The folder-title
// runtime links a separate libc PRX, so route this adapter to libkernel's
// exported syscall entry instead of leaving an unresolved libc PRX import.
extern int _openat(int directory, const char* path, int flags, mode_t mode);

int __wrap_openat(int directory, const char* path, int flags, ...)
{
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = (mode_t)va_arg(args, int);
        va_end(args);
    }
    return _openat(directory, path, flags, mode);
}
