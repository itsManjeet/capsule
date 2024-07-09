#include <Interpreter.h>
using namespace SrcLang;

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

#define SYSERROR SRCLANG_VALUE_SET_REF(SRCLANG_VALUE_ERROR(strerror(errno)))
#define SAFE_SYSCALL(call) \
    if ((call) == -1) return SYSERROR;

SRCLANG_MODULE_FUNC(size) {
    SRCLANG_CHECK_ARGS_EXACT(0);

    winsize size;

    SAFE_SYSCALL(ioctl(STDIN_FILENO, TIOCGWINSZ, &size));
    auto geometry = new SrcLangMap();
    (*geometry)["width"] = SRCLANG_VALUE_NUMBER(size.ws_col);
    (*geometry)["height"] = SRCLANG_VALUE_NUMBER(size.ws_row);

    return SRCLANG_VALUE_MAP(geometry);
}

SRCLANG_MODULE_FUNC(activate) {
    SRCLANG_CHECK_ARGS_EXACT(0);

    termios attr;
    SAFE_SYSCALL(tcgetattr(STDIN_FILENO, &attr));
    attr.c_iflag &= ~(ECHO | ICANON);
    attr.c_cc[VMIN] = 1;
    attr.c_cc[VTIME] = 0;

    SAFE_SYSCALL(tcsetattr(STDIN_FILENO, TCSANOW, &attr));
    return SRCLANG_VALUE_TRUE;
}

SRCLANG_MODULE_FUNC(deactivate) {
    SRCLANG_CHECK_ARGS_EXACT(0);

    termios attr;
    SAFE_SYSCALL(tcgetattr(STDIN_FILENO, &attr));
    attr.c_iflag &= (ECHO | ICANON);

    SAFE_SYSCALL(tcsetattr(STDIN_FILENO, TCSANOW, &attr));
    return SRCLANG_VALUE_TRUE;
}

SRCLANG_MODULE_INIT {
    SRCLANG_MODULE_DEFINE_FUNC(size);
    SRCLANG_MODULE_DEFINE_FUNC(activate);
    SRCLANG_MODULE_DEFINE_FUNC(deactivate);
}