/* Native application POSIX support: preserve the actual ioctl error. */
#ifndef PS5_NATIVE_GPU
#error "Compile native POSIX support only for PS5_NATIVE_GPU"
#endif
#include <termios.h>
#include <sys/ioctl.h>

int isatty(int descriptor)
{
    struct termios attributes;
    return ioctl(descriptor, TIOCGETA, &attributes) == 0;
}
