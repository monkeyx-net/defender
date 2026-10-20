/*
 * The player's saved files (the arcade's CMOS: settings, audits and high
 * scores; graphics settings). Directory: $DEFENDER_SAVE_DIR, else
 * $XDG_DATA_HOME/defender, else (Windows) %APPDATA%/defender, else
 * ~/.local/share/defender. Files are written to a temp file and renamed, so a
 * power-off mid-write keeps the old one.
 */
#ifndef DEFENDER_SAVEFILE_H
#define DEFENDER_SAVEFILE_H

#include <stddef.h>

/* The full path of a saved file (static buffer). */
const char *save_path(const char *name);
/* Read exactly len bytes; returns 0, or -1 if missing or the wrong size. */
int save_load(const char *name, void *buf, size_t len);
/* Returns 0 on success. */
int save_store(const char *name, const void *buf, size_t len);

#endif
