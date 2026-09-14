#include "scmd/keywords.h"

#include <string.h>

typedef struct KeywordDef {
    const char *text;
    ScmdTokenKind kind;
} KeywordDef;

static const KeywordDef english_keywords[] = {
    {"get", TOK_GET},
    {"var", TOK_VAR},
    {"bool", TOK_BOOL},
    {"u8", TOK_U8},
    {"function", TOK_FUNCTION},
    {"if", TOK_IF},
    {"else", TOK_ELSE},
    {"while", TOK_WHILE},
    {"for", TOK_FOR},
    {"return", TOK_RETURN},
    {"true", TOK_TRUE},
    {"false", TOK_FALSE},
    {"wait", TOK_WAIT},
    {"block", TOK_BLOCK_KW},
    {"record", TOK_RECORD},
    {"jump", TOK_JUMP},
    {"ms", TOK_MS},
    {"s", TOK_S},
    {"tick", TOK_TICK},
    {"ticks", TOK_TICKS},
};

/* Temporary source compatibility for the 0.5 prototype. */
static const KeywordDef compatibility_keywords[] = {
    {"import", TOK_GET},
    {"fn", TOK_FUNCTION},
    {"func", TOK_FUNCTION},
    {"bit", TOK_BOOL},
};

static ScmdTokenKind lookup(const KeywordDef *defs, size_t count, const char *text, size_t len) {
    for (size_t i = 0; i < count; ++i) {
        size_t n = strlen(defs[i].text);
        if (n == len && memcmp(defs[i].text, text, len) == 0) return defs[i].kind;
    }
    return TOK_IDENT;
}

ScmdTokenKind scmd_keyword_lookup(const char *text, size_t len) {
    ScmdTokenKind k = lookup(english_keywords,
                             sizeof(english_keywords) / sizeof(english_keywords[0]),
                             text, len);
    if (k != TOK_IDENT) return k;
    return lookup(compatibility_keywords,
                  sizeof(compatibility_keywords) / sizeof(compatibility_keywords[0]),
                  text, len);
}
