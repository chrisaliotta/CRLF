#pragma once

#include <QTextBlock>

// The single QTextBlockUserData subclass in the application. This must stay
// singular: QTextBlock::setUserData() deletes the previous object, so two
// subsystems attaching their own data would silently free each other's.
// Written by the Highlighter, read by folding and the gutter.
class BlockData : public QTextBlockUserData
{
public:
    static BlockData *get(const QTextBlock &block)
    {
        auto *data = static_cast<BlockData *>(block.userData());
        if (!data) {
            data = new BlockData;
            const_cast<QTextBlock &>(block).setUserData(data);
        }
        return data;
    }

    int foldingIndent = 0;
};
