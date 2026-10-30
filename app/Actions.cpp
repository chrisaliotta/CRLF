#include "app/Actions.h"

#include <QKeySequence>

Actions::Actions(QObject *parent)
{
    const auto make = [parent](const QString &text, const QKeySequence &shortcut) {
        auto *action = new QAction(text, parent);
        if (!shortcut.isEmpty())
            action->setShortcut(shortcut);
        return action;
    };

    newFile = make(QStringLiteral("&New"), QKeySequence::New);
    open = make(QStringLiteral("&Open..."), QKeySequence::Open);
    openFolder = make(QStringLiteral("Open &Folder..."), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    openWslFolder = make(QStringLiteral("Open &WSL Folder..."), QKeySequence());
    save = make(QStringLiteral("&Save"), QKeySequence::Save);
    saveAs = make(QStringLiteral("Save &As..."), QKeySequence::SaveAs);
    persistEdits = make(QStringLiteral("&Remember Unsaved Edits"), QKeySequence());
    persistEdits->setCheckable(true);
    persistEdits->setChecked(true);
    closeTab = make(QStringLiteral("&Close Tab"), QKeySequence::Close);
    quit = make(QStringLiteral("E&xit"), QKeySequence::Quit);

    undo = make(QStringLiteral("&Undo"), QKeySequence::Undo);
    redo = make(QStringLiteral("&Redo"), QKeySequence::Redo);
    cut = make(QStringLiteral("Cu&t"), QKeySequence::Cut);
    copy = make(QStringLiteral("&Copy"), QKeySequence::Copy);
    paste = make(QStringLiteral("&Paste"), QKeySequence::Paste);
    find = make(QStringLiteral("&Find/Replace..."), QKeySequence::Find);
    gotoLine = make(QStringLiteral("&Go to Line..."), QKeySequence(Qt::CTRL | Qt::Key_G));

    explorer = make(QStringLiteral("&Explorer"), QKeySequence(Qt::CTRL | Qt::Key_B));
    explorer->setCheckable(true);
    wordWrap = make(QStringLiteral("&Word Wrap"), QKeySequence());
    wordWrap->setCheckable(true);
    hexMode = make(QStringLiteral("&Hex Editor"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H));
    hexMode->setCheckable(true);
    byteColumns = make(QStringLiteral("Byte &Columns..."), QKeySequence());
    byteColumns->setEnabled(false);
    zoomIn = make(QStringLiteral("Zoom &In"), QKeySequence::ZoomIn);
    zoomOut = make(QStringLiteral("Zoom &Out"), QKeySequence::ZoomOut);
    zoomReset = make(QStringLiteral("&Reset Zoom"), QKeySequence(Qt::CTRL | Qt::Key_0));

    foldAll = make(QStringLiteral("&Fold All"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketLeft));
    unfoldAll = make(QStringLiteral("&Unfold All"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketRight));
}

void Actions::setTextEditingEnabled(bool enabled)
{
    for (QAction *action : {undo, redo, cut, copy, paste, find, gotoLine, wordWrap, zoomIn,
                            zoomOut, zoomReset, foldAll, unfoldAll})
        action->setEnabled(enabled);
}
