#include "editor/FoldController.h"

#include "editor/BlockData.h"
#include "editor/CodeEditor.h"
#include "editor/FoldingLayout.h"

namespace {

void setBlockHidden(QTextBlock &block, bool hidden)
{
    block.setVisible(!hidden);
    // The scrollbar range is measured in lines, so a hidden block must
    // report zero lines or the range stays too long by the hidden count.
    block.setLineCount(hidden ? 0 : qMax(1, block.layout()->lineCount()));
}

} // namespace

FoldController::FoldController(CodeEditor *editor, FoldingLayout *layout)
    : m_editor(editor)
    , m_layout(layout)
{
}

bool FoldController::isFoldHeader(const QTextBlock &block)
{
    const QTextBlock next = block.next();
    return next.isValid()
           && BlockData::get(block)->foldingIndent < BlockData::get(next)->foldingIndent;
}

void FoldController::toggle(const QTextBlock &header)
{
    setFolded(header, header.next().isVisible());
}

void FoldController::setFolded(const QTextBlock &header, bool folded)
{
    const int indent = BlockData::get(header)->foldingIndent;
    // The header itself and the document's final block are never hidden.
    for (QTextBlock b = header.next(); b.isValid() && b.next().isValid(); b = b.next()) {
        if (BlockData::get(b)->foldingIndent <= indent)
            break;
        setBlockHidden(b, folded);
    }
    refresh();
}

void FoldController::foldAll()
{
    QTextBlock b = m_editor->document()->firstBlock().next();
    for (; b.isValid() && b.next().isValid(); b = b.next())
        setBlockHidden(b, BlockData::get(b)->foldingIndent > 0);
    refresh();
}

void FoldController::unfoldAll()
{
    for (QTextBlock b = m_editor->document()->firstBlock(); b.isValid(); b = b.next())
        setBlockHidden(b, false);
    refresh();
}

void FoldController::refresh()
{
    m_editor->moveCursorOutOfHiddenBlock();
    m_layout->requestUpdate();
    m_layout->notifyDocumentSizeChanged();
}
