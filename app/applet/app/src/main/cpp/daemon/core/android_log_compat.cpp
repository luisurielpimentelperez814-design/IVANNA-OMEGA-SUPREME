#include <cstdio>
#include <cstdarg>

extern "C" int __android_log_print(int priority,
                                   const char* tag,
                                   const char* fmt,
                                   ...)
{
    (void)priority;

    va_list args;
    va_start(args, fmt);

    std::fprintf(stderr, "[%s] ", tag ? tag : "ANDROID");
    int result = std::vfprintf(stderr, fmt, args);
    std::fprintf(stderr, "\n");

    va_end(args);
    return result;
}
