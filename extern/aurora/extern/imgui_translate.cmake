# Port: the shown texts of ImGui translated. imgui.cpp and imgui_widgets.cpp are
# copied into the build folder with a hook (gImGuiTranslateHook, in
# imgui_item_hook.cpp) where ImGui measures and draws a text and where it
# formats one, and the copies are compiled in place of the originals. The
# labels stay English in the code, as ids and for the settings search; a text
# with no translation shows as it is. The translations are the port's
# (platform/port_translate.cpp).

# Puts `hook` before `before`, which must be in `text_var` once.
function(imgui_translate_insert text_var before hook)
  set(text "${${text_var}}")
  string(FIND "${text}" "${before}" first)
  string(FIND "${text}" "${before}" last REVERSE)
  if (first EQUAL -1 OR NOT first EQUAL last)
    message(FATAL_ERROR "imgui_translate: ImGui no longer has this text once:\n${before}")
  endif ()
  string(REPLACE "${before}" "${hook}${before}" text "${text}")
  set(${text_var} "${text}" PARENT_SCOPE)
endfunction()

# Writes the copy only when it changed, so that it is not compiled again.
function(imgui_translate_write name text)
  set(copy "${CMAKE_CURRENT_BINARY_DIR}/imgui_translate/${name}")
  set(previous "")
  if (EXISTS "${copy}")
    file(READ "${copy}" previous)
  endif ()
  if (NOT previous STREQUAL text)
    file(WRITE "${copy}" "${text}")
  endif ()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${imgui_SOURCE_DIR}/${name}" "${CMAKE_CURRENT_LIST_DIR}/imgui_translate.cmake")
endfunction()

set(_hook "// port: the shown texts translated (imgui_translate.cmake)
extern const char* (*gImGuiTranslateHook)(const char* text, const char* text_end, const char** out_end);
static inline void PortTranslateText(const char** text, const char** text_end)
{
    if (gImGuiTranslateHook != NULL && *text != *text_end)
        *text = gImGuiTranslateHook(*text, *text_end, text_end);
}
static inline const char* PortTranslateFormat(const char* fmt)
{
    if (gImGuiTranslateHook == NULL || fmt == NULL || fmt[0] == 0)
        return fmt;
    const char* end = fmt + ImStrlen(fmt);
    return gImGuiTranslateHook(fmt, end, &end);
}

")
set(_text "    PortTranslateText(&text, &text_display_end);\n")

file(READ "${imgui_SOURCE_DIR}/imgui.cpp" _imgui)
imgui_translate_insert(_imgui
  "void ImGui::RenderText(ImVec2 pos, const char* text, const char* text_end, bool hide_text_after_hash)\n"
  "${_hook}")
# RenderText
imgui_translate_insert(_imgui
  "    if (text != text_display_end)\n    {\n        window->DrawList->AddText(g.Font, g.FontSize, pos, GetColorU32(ImGuiCol_Text), text, text_display_end);"
  "${_text}")
# RenderTextWrapped
imgui_translate_insert(_imgui
  "    if (text != text_end)\n    {\n        window->DrawList->AddText(g.Font, g.FontSize, pos, GetColorU32(ImGuiCol_Text), text, text_end, wrap_width);"
  "    PortTranslateText(&text, &text_end);\n")
# RenderTextClipped
imgui_translate_insert(_imgui
  "    const int text_len = (int)(text_display_end - text);\n    if (text_len == 0)\n        return;\n\n    ImGuiContext& g = *GImGui;\n    ImGuiWindow* window = g.CurrentWindow;\n    RenderTextClippedEx("
  "${_text}")
# RenderTextEllipsis (tabs, table headers, separator texts)
imgui_translate_insert(_imgui
  "    const ImVec2 text_size = text_size_if_known ? *text_size_if_known : CalcTextSize(text, text_end_full, false, 0.0f);\n\n    //draw_list->AddLine("
  "    PortTranslateText(&text, &text_end_full);\n")
# CalcTextSize
imgui_translate_insert(_imgui
  "    ImFont* font = g.Font;\n    const float font_size = g.FontSize;\n    if (text == text_display_end)\n        return ImVec2(0.0f, font_size);\n    ImVec2 text_size = font->CalcTextSizeA("
  "    if (gImGuiTranslateHook != NULL && text_display_end == NULL)\n        text_display_end = text + ImStrlen(text);\n${_text}")
imgui_translate_write(imgui.cpp "${_imgui}")

file(READ "${imgui_SOURCE_DIR}/imgui_widgets.cpp" _imgui_widgets)
imgui_translate_insert(_imgui_widgets
  "void ImGui::TextV(const char* fmt, va_list args)\n"
  "${_hook}")
# TextV (Text, TextColored, TextDisabled, TextWrapped and the tooltips)
imgui_translate_insert(_imgui_widgets
  "    const char* text, *text_end;\n    ImFormatStringToTempBufferV(&text, &text_end, fmt, args);\n    TextEx(text, text_end, ImGuiTextFlags_NoWidthForLargeClippedText);"
  "    fmt = PortTranslateFormat(fmt);\n")
# BulletTextV
imgui_translate_insert(_imgui_widgets
  "    const char* text_begin, *text_end;\n    ImFormatStringToTempBufferV(&text_begin, &text_end, fmt, args);\n    const ImVec2 label_size = CalcTextSize(text_begin, text_end, false);"
  "    fmt = PortTranslateFormat(fmt);\n")
# InputTextEx's hint
imgui_translate_insert(_imgui_widgets
  "    }\n\n    // Render text. We currently only render selection"
  "        PortTranslateText(&buf_display, &buf_display_end);\n")
imgui_translate_write(imgui_widgets.cpp "${_imgui_widgets}")

set(IMGUI_TRANSLATE_SOURCES
  "${CMAKE_CURRENT_BINARY_DIR}/imgui_translate/imgui.cpp"
  "${CMAKE_CURRENT_BINARY_DIR}/imgui_translate/imgui_widgets.cpp")
