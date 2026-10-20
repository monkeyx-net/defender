/*
 * Raw deflate decoder (RFC 1951), no dependencies; used for PNG image data
 * and zip archives (the user's ROM set).
 */
#ifndef DEFENDER_INFLATE_H
#define DEFENDER_INFLATE_H

#include <stddef.h>
#include <stdint.h>

/* Inflate `len` bytes of raw deflate data into a malloc'd buffer. Returns 0, or -1 with *err set. */
int inflate_raw(const uint8_t *in, size_t len, uint8_t **out, size_t *out_len, const char **err);

#endif
