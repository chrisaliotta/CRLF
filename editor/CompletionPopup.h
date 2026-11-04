#pragma once

#include "editor/CompletionItem.h"

#include <QFrame>

class QLabel;
class QListWidget;

// The list under the caret. The editor keeps focus; this window does not.
class CompletionPopup : public QFrame
{
    Q_OBJECT

public:
    explicit CompletionPopup(QWidget *parent = nullptr);

    void setItems(const QList<CompletionItem> &items);
    void setHighlighting(const LanguageRegistry *registry, const Theme *theme);
    void moveRow(int delta);
    void acceptCurrent();
    bool isEmpty() const;

signals:
    void accepted(const CompletionItem &item);

private:
    void showDocumentation(int row);

    QListWidget *m_list = nullptr;
    QLabel *m_documentation = nullptr;
    QList<CompletionItem> m_items;
    const LanguageRegistry *m_registry = nullptr;
    const Theme *m_theme = nullptr;
};
