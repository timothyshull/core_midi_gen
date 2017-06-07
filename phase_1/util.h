#ifndef CORE_MIDI_GEN2_UTIL_H
#define CORE_MIDI_GEN2_UTIL_H

#include <AudioToolbox/AudioToolbox.h>
#include <istream>
#include <sstream>

void check_error(OSStatus error, const char operation[]);

class bad_lexical_cast : public std::exception {};

// Boost/http://www.drdobbs.com/sutters-mill-the-string-formatters-of-ma/184401458
template<typename Target, typename Source>
Target lexical_cast(Source arg)
{
    std::stringstream interpreter;
    Target result;

    if (!(interpreter << arg) || !(interpreter >> result) || !(interpreter >> std::ws).eof()) {
        throw bad_lexical_cast{};
    }

    return result;
}

#endif //CORE_MIDI_GEN2_UTIL_H
