#include "port_translate.h"

#include "port_env.h"
#include "port_json.h"
#include "port_log.h"

#include <SDL3/SDL_locale.h>
#include <SDL3/SDL_stdinc.h>

#include <string>
#include <string_view>
#include <unordered_map>

// ImGui's hook (extern/aurora/extern/imgui_item_hook.cpp).
extern const char* (*gImGuiTranslateHook)(const char* text, const char* text_end, const char** out_end);

namespace port {
namespace {

std::string sLanguage;
// The file's values stay where the parser put them; the table points into them.
PortJson::Value sFile;
std::unordered_map< std::string_view, const std::string* > sTable;

// A printf format's conversions, in order ("%d", "%.1f", "%s" -> "dfs"), so that
// a translation that would read its arguments differently is left out. A '%'
// that starts no conversion ("100% is the original") is text.
std::string Conversions(std::string_view text) {
  std::string conversions;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '%') {
      continue;
    }
    std::string widths;
    size_t j = i + 1;
    while (j < text.size() && std::string_view("-+#0123456789.*hlLzjt").find(text[j]) != std::string_view::npos) {
      if (text[j] == '*') {
        widths += '*';
      }
      ++j;
    }
    if (j < text.size() && std::string_view("%diouxXeEfFgGaAcspn").find(text[j]) != std::string_view::npos) {
      conversions += widths;
      conversions += text[j];
      i = j;
    }
  }
  return conversions;
}

const char* Hook(const char* text, const char* text_end, const char** out_end) {
  const auto it = sTable.find(std::string_view(text, static_cast< size_t >(text_end - text)));
  if (it == sTable.end()) {
    *out_end = text_end;
    return text;
  }
  *out_end = it->second->data() + it->second->size();
  return it->second->c_str();
}

const TranslationFile* FindFile(const std::string& language) {
  for (unsigned i = 0; i < kTranslationFileCount; ++i) {
    if (language == kTranslationFiles[i].language) {
      return &kTranslationFiles[i];
    }
  }
  return nullptr;
}

const TranslationFile* SystemFile() {
  int count = 0;
  SDL_Locale** locales = SDL_GetPreferredLocales(&count);
  const TranslationFile* file = nullptr;
  for (int i = 0; locales != nullptr && i < count && file == nullptr; ++i) {
    const std::string language = locales[i]->language != nullptr ? locales[i]->language : "";
    if (language == "en") {
      break;
    }
    file = FindFile(language);
  }
  SDL_free(locales);
  return file;
}

} // namespace

void InitTranslations() {
  static bool sDone = false;
  if (sDone) {
    return;
  }
  sDone = true;
  const char* chosen = port::EnvString("MP_UI_LANGUAGE");
  const TranslationFile* file = chosen != nullptr ? FindFile(chosen) : SystemFile();
  if (file == nullptr) {
    return;
  }
  size_t errorOffset = 0;
  const char* errorReason = "";
  const std::string text(reinterpret_cast< const char* >(file->data), file->size);
  if (!PortJson::Parse(text, sFile, errorOffset, &errorReason) || !sFile.IsObject()) {
    PortLog::Write("port: translations/%s.json: %s at byte %zu\n", file->language,
                   sFile.IsObject() ? errorReason : "not an object", errorOffset);
    return;
  }
  size_t skipped = 0;
  for (const auto& [english, value] : sFile.AsObject()) {
    if (!value.IsString() || value.AsString().empty() || Conversions(english) != Conversions(value.AsString())) {
      ++skipped;
      continue;
    }
    sTable.emplace(std::string_view(english), &value.AsString());
  }
  if (skipped != 0) {
    PortLog::Write("port: translations/%s.json: %zu text(s) left in English (empty, or not the "
                   "same %% conversions)\n",
                   file->language, skipped);
  }
  sLanguage = file->language;
  gImGuiTranslateHook = Hook;
  PortLog::Write("port: menus in %s (%zu texts)\n", file->language, sTable.size());
}

const char* Translate(const char* text) {
  if (text == nullptr || sTable.empty()) {
    return text;
  }
  const auto it = sTable.find(std::string_view(text));
  return it == sTable.end() ? text : it->second->c_str();
}

const char* TranslationLanguage() { return sLanguage.c_str(); }

} // namespace port
