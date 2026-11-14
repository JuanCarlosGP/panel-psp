#ifndef PANEL_LANGUAGE_H
#define PANEL_LANGUAGE_H

#include <stdbool.h>

enum PanelLanguage {
    PANEL_SPANISH,
    PANEL_ENGLISH,
};

enum PanelLanguage load_language(enum PanelLanguage fallback);
bool save_language(enum PanelLanguage language);

#endif
