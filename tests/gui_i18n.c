/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The menu's languages: a catalog's search (either byte order), catalogs that are not whole ones, the language picked
   from the system's locales, and English wherever a language has no translation. The languages linked are the test's
   own, tests/po, through scripts/build-languages.py. */
#include "gui/i18n.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define MO_MAX 1024
/* a catalog of the pairs, sorted as they come, little- or big-endian */
typedef struct {
    unsigned char bytes[MO_MAX];
    size_t size;
} Mo;
static void word(Mo* mo, size_t at, unsigned long v, int big) {
    int i;
    for (i = 0; i < 4; ++i) mo->bytes[at + (size_t)i] = (unsigned char)(v >> (big ? 24 - 8 * i : 8 * i));
}
static void mo_make(Mo* mo, const char* const (*pairs)[2], int n, int big) {
    size_t at = 28 + 16 * (size_t)n;
    int side, i;
    memset(mo, 0, sizeof *mo);
    word(mo, 0, 0x950412deUL, big);
    word(mo, 8, (unsigned long)n, big);
    word(mo, 12, 28, big);
    word(mo, 16, 28 + 8 * (unsigned long)n, big);
    word(mo, 24, (unsigned long)at, big);
    for (side = 0; side < 2; ++side)
        for (i = 0; i < n; ++i) {
            size_t length = strlen(pairs[i][side]);
            assert(at + length + 1 <= MO_MAX);
            word(mo, 28 + 8 * (size_t)n * (size_t)side + 8 * (size_t)i, (unsigned long)length, big);
            word(mo, 28 + 8 * (size_t)n * (size_t)side + 8 * (size_t)i + 4, (unsigned long)at, big);
            memcpy(mo->bytes + at, pairs[i][side], length + 1);
            at += length + 1;
        }
    mo->size = at;
}
static const char* look(const Mo* mo, const char* s) { return snx_mo_lookup(mo->bytes, mo->size, s); }

static const char* const pairs[][2] = {{"", "Content-Type: text/plain; charset=UTF-8\n"},
                                       {"Add a folder", "Adicionar pasta"},
                                       {"Back", "Voltar"},
                                       {"Exit", "Sair"},
                                       {"Games", "Jogos"},
                                       {"Settings", "Configura\303\247\303\265es"},
                                       {"Text size", "Tamanho do texto"},
                                       {"Theme", "Tema"}};
#define PAIRS ((int)(sizeof pairs / sizeof *pairs))

static void search(void) {
    Mo mo;
    int big, n, i;
    for (big = 0; big < 2; ++big) {
        mo_make(&mo, pairs, PAIRS, big);
        for (i = 0; i < PAIRS; ++i) assert(!strcmp(look(&mo, pairs[i][0]), pairs[i][1]));
        /* before the first, between two, after the last */
        assert(!look(&mo, " ") && !look(&mo, "Backs") && !look(&mo, "F") && !look(&mo, "Zebra"));
        assert(!look(&mo, "back") && !look(&mo, "Them")); /* bytes, not letters */
        /* every catalog size: the search's halves, odd and even */
        for (n = 1; n <= PAIRS; ++n) {
            mo_make(&mo, pairs, n, big);
            for (i = 0; i < n; ++i) assert(!strcmp(look(&mo, pairs[i][0]), pairs[i][1]));
            for (i = n; i < PAIRS; ++i) assert(!look(&mo, pairs[i][0]));
        }
    }
}
/* a catalog cut, or one whose numbers point out of it: no translation, nothing read past it */
static void malformed(void) {
    Mo mo, bad;
    size_t cut;
    mo_make(&mo, pairs, PAIRS, 0);
    assert(!snx_mo_lookup(NULL, 0, "Back") && !snx_mo_lookup(mo.bytes, 27, "Back"));
    for (cut = 0; cut < mo.size; ++cut) { /* every shorter size: a string, or a table, past the end */
        const char* s = snx_mo_lookup(mo.bytes, cut, "Theme");
        assert(!s || (size_t)(s - (const char*)mo.bytes) + strlen(s) < cut);
    }
    bad = mo;
    bad.bytes[0] ^= 1; /* no magic in either order */
    assert(!look(&bad, "Back"));
    bad = mo;
    word(&bad, 8, 0, 0); /* no entries */
    assert(!look(&bad, ""));
    bad = mo;
    word(&bad, 8, 0x7fffffffUL, 0); /* more entries than bytes */
    assert(!look(&bad, "Back"));
    bad = mo;
    word(&bad, 12, (unsigned long)mo.size, 0); /* the originals past the end */
    assert(!look(&bad, "Back"));
    bad = mo;
    word(&bad, 16, 30, 0); /* the translations not on a word */
    assert(!look(&bad, "Back"));
    /* the translations' table whole only by half: musl's own test lets 4 bytes an entry through, and reads on */
    bad = mo;
    cut = (mo.size - 4 * PAIRS - 1) & ~(size_t)3;
    word(&bad, 16, (unsigned long)cut, 0);
    memcpy(bad.bytes + cut, mo.bytes + 28 + 8 * PAIRS, 8 * PAIRS); /* what such a reader would find there */
    assert(!look(&bad, "Back"));
    /* the originals' table as well, and a whole one not on a word: a copy of it where such a reader would look */
    bad = mo;
    cut = (mo.size - 4 * PAIRS - 1) & ~(size_t)3;
    word(&bad, 12, (unsigned long)cut, 0);
    memcpy(bad.bytes + cut, mo.bytes + 28, 8 * PAIRS);
    assert(!look(&bad, "Back"));
    bad = mo;
    cut = ((mo.size + 3) & ~(size_t)3) + 2;
    bad.size = cut + 8 * PAIRS;
    word(&bad, 12, (unsigned long)cut, 0);
    memcpy(bad.bytes + cut, mo.bytes + 28, 8 * PAIRS);
    assert(!look(&bad, "Back"));
    bad = mo;
    word(&bad, 28 + 8 * 2 + 4, (unsigned long)mo.size, 0); /* an original's string past the end */
    assert(!look(&bad, "Back"));
    bad = mo;
    word(&bad, 28 + 8 * 2, (unsigned long)mo.size, 0); /* its length past the end */
    assert(!look(&bad, "Back"));
    bad = mo;
    word(&bad, 28 + 8 * PAIRS + 8 * 2, 3, 0); /* a translation whose length does not end at its NUL */
    assert(!look(&bad, "Back") && !strcmp(look(&bad, "Exit"), "Sair"));
}
/* the languages tests/po makes: English first, no catalog; then xx, its name in UTF-8 */
static void languages(void) {
    assert(snx_gui_language_count == 2);
    assert(!strcmp(snx_gui_languages[0].code, "en") && !snx_gui_languages[0].mo && !snx_gui_languages[0].size);
    assert(!strcmp(snx_gui_languages[1].code, "xx") && !strcmp(snx_gui_languages[1].name, "Test\303\252s"));
    snx_tr_languages(snx_gui_languages, snx_gui_language_count);
    assert(snx_tr_language() == 0 && !strcmp(snx_tr("Settings"), "Settings"));
    assert(snx_tr_find("xx") == 1 && snx_tr_find("en") == 0 && snx_tr_find("pt_BR") == -1 && snx_tr_find("") == -1);
    snx_tr_use(1);
    assert(snx_tr_language() == 1);
    assert(!strcmp(snx_tr("Settings"), "Ajustes") && !strcmp(snx_tr("Language"), "L\303\255ngua"));
    {
        char line[64];
        snprintf(line, sizeof line, snx_tr("Auto: %s"), "English");
        assert(!strcmp(line, "Autom\303\241tico: English"));
    }
    /* English back: a phrase it has not translated, one marked fuzzy, one it does not have, and the header's "" */
    assert(!strcmp(snx_tr("Exit"), "Exit") && !strcmp(snx_tr("Games"), "Games"));
    assert(!strcmp(snx_tr("Not a phrase of the menu"), "Not a phrase of the menu") && !strcmp(snx_tr(""), ""));
    snx_tr_use(0);
    assert(!strcmp(snx_tr("Settings"), "Settings"));
    snx_tr_use(2); /* past the list, or before it: English */
    assert(snx_tr_language() == 0 && !strcmp(snx_tr("Settings"), "Settings"));
    snx_tr_use(1);
    snx_tr_use(-1);
    assert(snx_tr_language() == 0);
}
/* a catalog whose translation is empty (msgfmt keeps none, another tool might): English, as for none */
static void empty(void) {
    static const char* const blank[][2] = {
        {"", "Content-Type: text/plain; charset=UTF-8\n"}, {"Back", ""}, {"Exit", "Sair"}};
    static Mo mo;
    SnxLanguage list[2] = {{"en", "English", NULL, 0}, {"xx", "Test", NULL, 0}};
    mo_make(&mo, blank, 3, 0);
    list[1].mo = mo.bytes;
    list[1].size = mo.size;
    assert(look(&mo, "Back") && !*look(&mo, "Back"));
    snx_tr_languages(list, 2);
    snx_tr_use(1);
    assert(!strcmp(snx_tr("Back"), "Back") && !strcmp(snx_tr("Exit"), "Sair"));
    snx_tr_use(0);
}
/* the system's locales, the most wanted first, to a language of the list */
static int pick(const SnxLanguage* list, int count, const char* const* locales, int n) {
    snx_tr_languages(list, count);
    return snx_tr_pick(locales, n);
}
static void choice(void) {
    static const SnxLanguage list[] = {{"en", "English", NULL, 0},
                                       {"de_AT", "Deutsch", NULL, 0},
                                       {"fr_CA", "Fran\303\247ais", NULL, 0},
                                       {"pt", "Portugu\303\252s", NULL, 0},
                                       {"pt_BR", "Portugu\303\252s (Brasil)", NULL, 0}};
    static const char *const brazil[] = {"pt_BR"}, *hyphen[] = {"pt-BR"}, *encoded[] = {"pt_BR.UTF-8"},
                             *portugal[] = {"pt_PT"}, *bare[] = {"pt"}, *german[] = {"de_DE"}, *french[] = {"fr"},
                             *order[] = {"ja_JP", "en_US", "pt_BR"}, *later[] = {"ja", "C", "pt_BR"}, *none[] = {"C"},
                             *empty[] = {""}, *modifier[] = {"de_AT@euro"};
    int count = (int)(sizeof list / sizeof *list);
    assert(pick(list, count, brazil, 1) == 4 && pick(list, count, hyphen, 1) == 4);
    assert(pick(list, count, encoded, 1) == 4 && pick(list, count, modifier, 1) == 1);
    /* no country of its own: the language's own (pt) before another country's */
    assert(pick(list, count, portugal, 1) == 3 && pick(list, count, bare, 1) == 3);
    assert(pick(list, count - 2, brazil, 1) == 0 && pick(list, count - 1, portugal, 1) == 3);
    /* only another country's: that one */
    assert(pick(list, count, german, 1) == 1 && pick(list, count, french, 1) == 2);
    {
        static const SnxLanguage brazil_only[] = {{"en", "English", NULL, 0}, {"pt_BR", "Portugu\303\252s", NULL, 0}};
        static const SnxLanguage brazil_first[] = {
            {"en", "English", NULL, 0}, {"pt_BR", "Portugu\303\252s", NULL, 0}, {"pt", "Portugu\303\252s", NULL, 0}};
        assert(pick(brazil_only, 2, portugal, 1) == 1 && pick(brazil_only, 2, bare, 1) == 1);
        assert(pick(brazil_first, 3, portugal, 1) == 2); /* the one with no country, wherever it is in the list */
    }
    /* the first the list has, in the order wanted: English counts, and one it lacks is passed over */
    assert(pick(list, count, order, 3) == 0 && pick(list, count, later, 3) == 4);
    assert(pick(list, count, none, 1) == 0 && pick(list, count, empty, 1) == 0 && pick(list, count, NULL, 0) == 0);
}
int main(void) {
    search();
    malformed();
    languages();
    empty();
    choice();
    puts("gui_i18n: ok");
    return 0;
}
