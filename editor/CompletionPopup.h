#pragma once

#include "editor/CompletionItem.h"

#include <QFrame>

class QListWidget;

// The list under the caret. The editor keeps focus; this window does not.
class CompletionPopup : public QFrame
{
    Q_OBJECT

public:
    explicit CompletionPopup(QWidget *parent = nullptr);

    void setItems(const QList<CompletionItem> &items);
    void moveRow(int delta);
    void acceptCurrent();
    bool isEmpty() const;

signals:
    void accepted(const CompletionItem &item);

private:
    QListWidget *m_list = nullptr;
    QList<CompletionItem> m_items;
};
