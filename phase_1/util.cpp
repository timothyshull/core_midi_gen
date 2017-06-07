#include <cstdint>
#include <cstring>
#include <cctype>
#include <iostream>

#include "util.h"

void check_error(OSStatus error, const char operation[])
{
    if (error == noErr) { return; }

    auto int_32_err = CFSwapInt32HostToBig(static_cast<uint32_t>(error));
    char str[7] = {'\'', 0, 0, 0, 0, '\'', '\0'};
    std::memcpy(str + 1, &int_32_err, sizeof(char) * 4);

    if (std::isprint(str[1]) && std::isprint(str[2]) && std::isprint(str[3]) && std::isprint(str[4])) {
        fprintf(stderr, "Error: %s (%s)", operation, str); // for speed
    } else {
        fprintf(stderr, "Error: %s (%d)", operation, error);
    }

    exit(1);
}