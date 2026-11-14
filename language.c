#include <fcntl.h>
#include <unistd.h>

#include "language.h"

// PSPSDK starts the app in the directory containing EBOOT.PBP.
static const char language_path[] = "panel-language.cfg";

enum PanelLanguage load_language(enum PanelLanguage fallback)
{
    int file = open(language_path, O_RDONLY);
    if (file < 0)
        return fallback;

    char value;
    ssize_t bytes = read(file, &value, sizeof value);
    close(file);
    if (bytes == sizeof value) {
        if (value == 's')
            return PANEL_SPANISH;
        if (value == 'e')
            return PANEL_ENGLISH;
    }
    return fallback;
}

bool save_language(enum PanelLanguage language)
{
    // Both settings fit in one byte. Overwrite it without truncating the file.
    int file = open(language_path, O_WRONLY | O_CREAT, 0666);
    if (file < 0)
        return false;

    char value = language == PANEL_ENGLISH ? 'e' : 's';
    ssize_t bytes = write(file, &value, sizeof value);
    int status = close(file);
    return bytes == sizeof value && status == 0;
}
