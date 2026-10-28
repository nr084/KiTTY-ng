/* ISO date from this translation unit's actual compilation date. */
#ifndef KITTYNG_BUILD_DATE_H
#define KITTYNG_BUILD_DATE_H
static const char *kr_build_date(void)
{
    static char result[11];
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *date = __DATE__;
    int month = 0;
    for (int i = 0; i < 12; i++)
        if (!strncmp(date, months + 3*i, 3)) { month = i + 1; break; }
    snprintf(result, sizeof(result), "%.4s-%02d-%02d", date + 7, month, atoi(date + 4));
    return result;
}
#endif
