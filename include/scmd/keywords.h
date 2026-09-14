#ifndef SCMD_KEYWORDS_H
#define SCMD_KEYWORDS_H

#include <stddef.h>
#include "scmd/lexer.h"

/*
 * Central keyword mapping for the source language.
 *
 * Current releases ship English spellings only. Future localization should add keyword
 * bundles here (or behind this API) while keeping Parser/AST token kinds
 * unchanged.
 */
ScmdTokenKind scmd_keyword_lookup(const char *text, size_t len);

#endif
