#pragma once

#include <QPlainTextDocumentLayout>

// QPlainTextEdit's scrollbar range only recomputes on the layout's protected
// documentSizeChanged signal. This subclass exists solely to expose it, which
// is required after toggling block visibility for folding.
class FoldingLayout : public QPlainTextDocumentLayout
{
public:
    using QPlainTextDocumentLayout::QPlainTextDocumentLayout;

    void notifyDocumentSizeChanged() { emit documentSizeChanged(documentSize()); }
};
