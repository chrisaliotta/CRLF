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

    FoldMode foldMode = FoldMode::Indent;
    QList<FoldPair> foldPairs;
};
