#include "kittyng-i18n.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
#include <stdio.h>
#include <string.h>
int main(void) {
    CHECK(kr_i18n_language_count()==2);
    CHECK(kr_i18n_language_id("en")==0);
    CHECK(kr_i18n_language_id("ru")==1);
    CHECK(kr_i18n_language_id("de")==-1);
    CHECK(!kr_i18n_language(2));
    CHECK(!kr_i18n_translate(1,KR_TEXT_WINDOWS,NULL));
    const char *key="Close all sessions in this window?";
    for (int c=KR_TEXT_WINDOWS;c<=KR_TEXT_MAC_UI;c++) {
        CHECK(!strcmp(kr_i18n_translate(0,c,key),key));
        const char *ru=kr_i18n_translate(1,c,key);
        CHECK(strcmp(ru,key));
        CHECK(!strcmp(kr_i18n_source(c,ru),key));
        CHECK(!strcmp(kr_i18n_source(c,key),key));
        CHECK(!strcmp(kr_i18n_translate(1,c,"user-defined data"),"user-defined data"));
        CHECK(!strcmp(kr_i18n_translate(42,c,key),key));
        for (int n=0;n<100;n++) CHECK(kr_i18n_translate(n%2,c,key));
        const char *fmt=kr_i18n_translate(1,c,"Reading key file \"%s\"");
        CHECK(strstr(fmt,"%s"));
        CHECK(strcmp(fmt,"Reading key file \"%s\""));
    }
    puts("PASS: shared built-in EN/RU catalog, fallback, formats, contexts and language round-trips");
    return 0;
}
