#pragma once

#include "Types.h"

#include <QRegularExpression>
#include <QString>

// Module-local types for the syntax engine.

struct StringRule {
    QString start;
    QString end;
    QString escape;
    bool multiline = false;
};

struct PatternRule {
    TokenRole role = TokenRole::Text;
    QRegularExpression regex;
    // Also colors matches inside strings, e.g. variables substituted into
    // quoted text, which the string pass would otherwise paint over.
    bool insideStrings = false;
};

// A region whose lines are highlighted by another language, e.g. a Markdown
// fenced code block. `start` captures the language hint (fence info string)
// in group `languageCapture`; lines up to one matching `end` belong to it.
struct EmbedRule {
    QRegularExpression start;
    QRegularExpression end;
    int languageCapture = 1;
};

// A declaration pattern. `capture` is the group that holds the defined name.
struct SymbolRule {
    QRegularExpression regex;
    int capture = 1;
    // Kept on the open document only, so a local variable is not a project-wide name.
    bool local = false;
    // `capture` is a parameter list; each name in it is a declaration.
    bool params = false;
    // Prepended to the captured name, such as `@` for a Sass mixin, so the
    // name does not share a key with an identifier from another language.
    QString prefix;
};
