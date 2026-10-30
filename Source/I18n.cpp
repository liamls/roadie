#include "I18n.h"

#include <atomic>
#include <string_view>
#include <unordered_map>

namespace i18n
{
namespace
{
std::atomic<Language> currentLanguage { Language::english };

// tr() runs for every string drawn: keys are the table's own literals, looked up without a copy.
const std::unordered_map<std::string_view, const char*>& frenchLookup()
{
    static const auto lookup = []
    {
        std::unordered_map<std::string_view, const char*> map;
        for (const auto& [english, french] : frenchTable())
            map.emplace (english, french);
        return map;
    }();
    return lookup;
}
} // namespace

Language loadSavedLanguage (juce::PropertiesFile& settings)
{
    return settings.getValue ("language", "en") == "fr" ? Language::french : Language::english;
}

void saveLanguage (juce::PropertiesFile& settings, Language language)
{
    settings.setValue ("language", language == Language::french ? "fr" : "en");
    settings.saveIfNeeded();
}

void setLanguage (Language language) { currentLanguage = language; }
Language getLanguage() { return currentLanguage; }

juce::String tr (const char* english)
{
    if (currentLanguage == Language::french)
    {
        const auto& lookup = frenchLookup();
        if (const auto found = lookup.find (english); found != lookup.end())
            return juce::String::fromUTF8 (found->second);
    }
    return juce::String::fromUTF8 (english);
}

juce::String percent() { return currentLanguage == Language::french ? juce::String::fromUTF8 ("\xc2\xa0%") : juce::String ("%"); }
juce::juce_wchar decimalPoint() { return currentLanguage == Language::french ? ',' : '.'; }
} // namespace i18n
