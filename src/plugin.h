#ifndef INKWELL_PLUGIN_H
#define INKWELL_PLUGIN_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INKWELL_PLUGIN_ABI 1

#if defined(_WIN32)
#define INKWELL_PLUGIN_EXPORT __declspec(dllexport)
#else
#define INKWELL_PLUGIN_EXPORT
#endif

enum {
    INKWELL_NOTE_SPELLING = 0,
    INKWELL_NOTE_GRAMMAR = 1,
    INKWELL_NOTE_STYLE = 2,
    INKWELL_NOTE_NAME = 3
};

typedef struct InkwellPluginNote {
    size_t begin;
    size_t end;
    int kind;
    const char* message;
    const char* const* replacements;
    size_t replacement_count;
} InkwellPluginNote;

typedef void (*InkwellPluginEmit)(void* user, const InkwellPluginNote* note);

typedef struct InkwellPlugin {
    int abi;
    const char* id;
    const char* name;
    void (*analyze)(const char* text, size_t length, void* user, InkwellPluginEmit emit);
} InkwellPlugin;

typedef const InkwellPlugin* (*InkwellPluginEntry)(void);

#ifdef __cplusplus
}
#endif

#endif
