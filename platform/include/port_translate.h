#ifndef METROID_PRIME_PORT_PORT_TRANSLATE_H
#define METROID_PRIME_PORT_PORT_TRANSLATE_H

// The port's menus in the system's language. translations/<language>.json maps
// each English text the menus show to its translation, and is built into the
// executable (cmake/PortTranslations.cmake). ImGui looks up every text it
// measures, draws or formats (extern/aurora/extern/imgui_translate.cmake), so
// the menus' code keeps its English labels, as ids and for the settings search.
// A text with no translation shows in English.
namespace port {

struct TranslationFile {
  const char* language; // "fr": translations/fr.json
  const unsigned char* data;
  unsigned size;
};
extern const TranslationFile kTranslationFiles[];
extern const unsigned kTranslationFileCount;

// The first of the system's preferred languages that has a translation, unless
// English comes first; MP_UI_LANGUAGE=<language> (or "en") chooses instead.
void InitTranslations();

// `text`'s translation, or `text`.
const char* Translate(const char* text);

// The language in use, "" for English.
const char* TranslationLanguage();

} // namespace port

#endif // METROID_PRIME_PORT_PORT_TRANSLATE_H
