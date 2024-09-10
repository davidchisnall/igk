
// Work around broken packages shipped by Homebrew
#ifdef __APPLE__
#define U_DISABLE_RENAMING 0
#endif

#include <unicode/uchar.h>
#include <unicode/utf8.h>
#include <unicode/utypes.h>
