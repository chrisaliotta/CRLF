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
};
