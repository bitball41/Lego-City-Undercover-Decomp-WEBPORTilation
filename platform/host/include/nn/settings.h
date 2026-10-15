#pragma once

#include <cstring>

#include "types.h"

namespace nn::settings {

enum Language {
    Language_Japanese,
    Language_English,
    Language_French,
    Language_German,
    Language_Italian,
    Language_Spanish,
    Language_Chinese,
    Language_Korean,
    Language_Dutch,
    Language_Portuguese,
    Language_Russian,
    Language_Taiwanese,
    Language_BritishEnglish,
    Language_CanadianFrench,
    Language_LatinAmericanSpanish
};

struct LanguageCode {
    char code[8];

    static LanguageCode Make(Language language) {
        static const char* kCodes[] = {
            "ja",    "en-US", "fr",    "de",    "it",    "es",    "zh-CN", "ko",
            "nl",    "pt",    "ru",    "zh-TW", "en-GB", "fr-CA", "es-419",
        };
        LanguageCode out{};
        if (language >= 0 && language < static_cast<int>(sizeof(kCodes) / sizeof(kCodes[0]))) {
            const char* text = kCodes[language];
            for (int i = 0; text[i] != '\0' && i < 8; ++i) {
                out.code[i] = text[i];
            }
        }
        return out;
    }
};

inline bool operator==(const LanguageCode& a, const LanguageCode& b) {
    return std::memcmp(a.code, b.code, 8) == 0;
}

inline bool operator==(Language language, const LanguageCode& code) {
    return LanguageCode::Make(language) == code;
}

inline bool operator==(const LanguageCode& code, Language language) {
    return language == code;
}

}  // namespace nn::settings
