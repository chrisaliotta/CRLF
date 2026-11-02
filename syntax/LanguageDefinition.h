#pragma once

#include "Types.h"
#include "syntax/SyntaxTypes.h"

#include <QList>
#include <QRegularExpression>
#include <QStringList>

// One language, parsed from a JSON file in resources/languages/.
class LanguageDefinition
{
public:
    static LanguageDefinition fromJsonFile(const QString &path);

    QString name;
    QStringList extensions;

    QString lineComment;
    QString blockCommentStart;
    QString blockCommentEnd;
    QList<StringRule> strings;

    // Invalid (empty-pattern) when the language declares no keywords/types.
    QRegularExpression keywordRegex;
    QRegularExpression typeRegex;
    QList<PatternRule> patterns;
    QList<EmbedRule> embedded;
    // Declaration patterns for Ctrl+click. Empty when the language has no
    // named types. `ignoreCase` folds both these patterns and name lookup.
    QList<SymbolRule> symbols;
    bool ignoreCase = false;
    // Import and export declarations are part of navigation. A name brought
    // in from another file jumps to that file instead of a same-named symbol
    // elsewhere in the project.
    bool modules = false;

    FoldMode foldMode = FoldMode::Indent;
    QList<FoldPair> foldPairs;
};
