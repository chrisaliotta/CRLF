#pragma once

#include <QAction>

// Every user-facing action with its shortcut, created once and shared by the
// menu bar and any other invokers.
struct Actions
{
    explicit Actions(QObject *parent);

    // Actions that only make sense on a text tab; disabled while a hex tab
    // is active.
    void setTextEditingEnabled(bool enabled);

    QAction *newFile;
    QAction *open;
    QAction *openFolder;
    QAction *save;
    QAction *saveAs;
    QAction *persistEdits;
    QAction *closeTab;
    QAction *quit;

    QAction *undo;
    QAction *redo;
    QAction *cut;
    QAction *copy;
    QAction *paste;
    QAction *find;
    QAction *gotoLine;

    QAction *explorer;
    QAction *wordWrap;
    QAction *hexMode;
    QAction *zoomIn;
    QAction *zoomOut;
    QAction *zoomReset;
    QAction *foldAll;
    QAction *unfoldAll;
};
