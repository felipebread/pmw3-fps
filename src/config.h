#pragma once
#include <errno.h>
#include <math.h>
#include <wchar.h>
#include <wctype.h>

/* Strict parsing prevents malformed settings from silently becoming zero. */
static int ParseTarget(const wchar_t *text, float *result)
{
    wchar_t *end;
    errno = 0;
    double value = wcstod(text, &end);
    if (end == text || errno == ERANGE || !isfinite(value)) return 0;
    while (iswspace(*end)) ++end;
    if (*end || value < 1.0 || value > 1000.0) return 0;
    *result = (float)value;
    return 1;
}
