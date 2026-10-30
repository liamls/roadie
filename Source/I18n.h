#pragma once

#include <juce_data_structures/juce_data_structures.h>

// Interface language. The source strings are English; French is looked up in a
// table (I18nFrench.cpp). The language is chosen once at start-up (settings key
// "language"), so changing it takes effect after a restart.
namespace i18n
{
enum class Language
{
    english,
    french
};

// Reads the saved language ("en" / "fr"), defaulting to English. When nothing
// has been saved yet the choice stays English, whatever the system language.
Language loadSavedLanguage (juce::PropertiesFile& settings);
void saveLanguage (juce::PropertiesFile& settings, Language language);

void setLanguage (Language language);
Language getLanguage();

// Translates an English UI string (UTF-8). Unknown strings are returned as is.
juce::String tr (const char* english);

// Number formatting follows the interface language: "50%" and "98.5" in
// English, "50 %" (no-break space) and "98,5" in French.
juce::String percent();
juce::juce_wchar decimalPoint();

// All English -> French pairs, for tests and tooling.
const std::vector<std::pair<const char*, const char*>>& frenchTable();
} // namespace i18n

using i18n::tr;
