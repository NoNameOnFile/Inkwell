#include "plugin.h"

#include <string.h>

static void analyze(const char* text, size_t length, void* user, InkwellPluginEmit emit) {
    const char needle[] = "plugin-mark";
    const size_t needle_length = sizeof(needle) - 1;
    if (text == nullptr || emit == nullptr || length < needle_length) {
        return;
    }
    for (size_t i = 0; i + needle_length <= length; ++i) {
        if (memcmp(text + i, needle, needle_length) != 0) {
            continue;
        }
        const char* replacements[] = {"marked"};
        InkwellPluginNote note;
        note.begin = i;
        note.end = i + needle_length;
        note.kind = INKWELL_NOTE_STYLE;
        note.message = "Sample plugin";
        note.replacements = replacements;
        note.replacement_count = 1;
        emit(user, &note);
        i += needle_length - 1;
    }
}

static const InkwellPlugin kPlugin = {INKWELL_PLUGIN_ABI, "sample", "Sample", analyze};

extern "C" INKWELL_PLUGIN_EXPORT const InkwellPlugin* inkwell_plugin(void) {
    return &kPlugin;
}
