/* The implementations of TinySoundFont and TinyMidiLoader.  This file is C,
 * not C++ (the rest of the SDK's .c files are compiled as C++): the library's
 * static tables then need no C++ runtime (__cxa_guard_*), which the link line
 * of the port does not have. */

#define TSF_IMPLEMENTATION
#include "tsf.h"
#define TML_IMPLEMENTATION
#include "tml.h"
