#pragma once

#include "editor/CompletionItem.h"

#include <QFrame>

class CompletionModel;
class CompletionRowDelegate;
class LanguageDefinition;
class LanguageRegistry;
class QListView;
class QTextBrowser;
class Theme;

// The list under the caret. The editor keeps focus; this window does not.
class CompletionPopup : public QFrame
{
    Q_OBJECT

public:
    explicit CompletionPopup(QWidget *parent = nullptr);

    // Selects the row the server marked as expected, or the first one.
    void setItems(const QList<CompletionItem> &items);
    // Shows the rows of the last setItems whose filter text starts with
    // prefix. An empty prefix shows every row. The comparison ignores case.
    void setPrefix(const QString &prefix);
    // Fills detail and documentation on the row whose data matches.
    void fillResolved(const QString &data, const QString &detail, const QString &documentation);
    void setHighlighting(const LanguageRegistry *registry, const Theme *theme,
                         const LanguageDefinition *language = nullptr);
    void moveRow(int delta);
    void acceptCurrent();
    // No row matches the prefix.
    bool isEmpty() const;

signals:
    void accepted(const CompletionItem &item);
    // The selected row has a server item and no documentation yet.
    void documentationRequested(const CompletionItem &item);

private:
    void refill(const QString &keepLabel);
    void showDocumentation(int row);

    QListView *m_list = nullptr;
    // The rows that match the prefix. The view asks it only for the rows on
    // screen, so a list of every type in scope costs no more than a short one.
    CompletionModel *m_model = nullptr;
    CompletionRowDelegate *m_delegate = nullptr;
    QFrame *m_separator = nullptr;
    QTextBrowser *m_documentation = nullptr;
    QList<CompletionItem> m_all;
    QString m_prefix;
    const LanguageRegistry *m_registry = nullptr;
    const Theme *m_theme = nullptr;
    const LanguageDefinition *m_language = nullptr;
};
