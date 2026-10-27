#pragma once

#include <QTextBlock>

class CodeEditor;
class FoldingLayout;

// Folds and unfolds regions using the foldingIndent values the highlighter
// stored in BlockData. Fold-open state is never stored anywhere: it is always
// read back as block.next().isVisible().
class FoldController
{
public:
    FoldController(CodeEditor *editor, FoldingLayout *layout);

    static bool isFoldHeader(const QTextBlock &block);

    void toggle(const QTextBlock &header);
    void setFolded(const QTextBlock &header, bool folded);
    void foldAll();
    void unfoldAll();

private:
    void refresh();

    CodeEditor *m_editor;
    FoldingLayout *m_layout;
};
