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
