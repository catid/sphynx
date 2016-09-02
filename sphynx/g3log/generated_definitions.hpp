// AUTO GENERATED MACRO DEFINITIONS FOR G3LOG

/** ==========================================================================
* 2015 by KjellKod.cc. This is PUBLIC DOMAIN to use at your own risk and comes
* with no warranties. This code is yours to share, use and modify with no
* strings attached and no restrictions or obligations.
* 
* For more information see g3log/LICENSE or refer refer to http://unlicense.org
* ============================================================================*/
#pragma once

// CMake induced definitions below. See g3log/Options.cmake for details.


#if defined(ANDROID) && !defined(DEFINED_TO_STRING)
#define DEFINED_TO_STRING
#include <sstream>
namespace std {
    template <typename T>
    std::string to_string(T value)
    {
        std::ostringstream os;
        os << value;
        return os.str();
    }
} // namespace std
#endif
