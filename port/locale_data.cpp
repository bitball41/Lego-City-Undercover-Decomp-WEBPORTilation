#include "nucore/NuLocaleManager.h"

#include <cstdint>

extern const char* const lang_name_japanese = "日本語";
extern const char* const lang_name_korean = "한국어";
extern const char* const lang_name_english = "English";
extern const char* const lang_name_american = "English (US)";
extern const char* const lang_name_german = "Deutsch";
extern const char* const lang_name_french = "Français";
extern const char* const lang_name_spanish = "Español";
extern const char* const lang_name_italian = "Italiano";
extern const char* const lang_name_dutch = "Nederlands";
extern const char* const lang_name_portuguese = "Português";
extern const char* const lang_name_danish = "Dansk";
extern const char* const lang_name_norwegian = "Norsk";
extern const char* const lang_name_swedish = "Svenska";
extern const char* const lang_name_finnish = "Suomi";
extern const char* const lang_name_greek = "Ελληνικά";
extern const char* const lang_name_czech = "Čeština";
extern const char* const lang_name_polish = "Polski";
extern const char* const lang_name_russian = "Русский";
extern const char* const lang_name_brazilian = "Português (BR)";
extern const char* const lang_name_chinese = "简体中文";
extern const char* const lang_name_hungarian = "Magyar";
extern const char* const lang_name_french_canadian = "Français (CA)";
extern const char* const lang_name_mexican = "Español (MX)";
extern const char* const lang_name_arabic = "العربية";
extern const char* const lang_name_chinese_traditional = "繁體中文";

static int s_languageIds[] = {static_cast<int>(NuLanguage::ENGLISH), static_cast<int>(NuLanguage::AMERICAN),
                              static_cast<int>(NuLanguage::FRENCH), static_cast<int>(NuLanguage::GERMAN),
                              static_cast<int>(NuLanguage::SPANISH), static_cast<int>(NuLanguage::ITALIAN), 0x19};

NuLanguage* NuLocaleManager::m_localisationLanguages[] = {reinterpret_cast<NuLanguage*>(s_languageIds)};

void* NuCore::GetApplicationState() {
    return nullptr;
}
