#include "nucore/NuLocaleManager.h"

#include <cstdio>

int TestLocale() {
    NuLocaleManager locale;
    const char* english = locale.GetEnglishName(locale.GetCurrentLanguage());
    if (english == nullptr || english[0] == '\0') {
        std::printf("FAIL locale name\n");
        return 1;
    }
    return 0;
}
