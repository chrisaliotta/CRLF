#include "editor/CompletionPopup.h"

#include <QListWidget>
#include <QVBoxLayout>

CompletionPopup::CompletionPopup(QWidget *parent)
    : QFrame(parent)
{
    setFocusPolicy(Qt::NoFocus);
    m_list = new QListWidget(this);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setMinimumWidth(280);
    m_list->setMaximumHeight(220);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_list);
    connect(m_list, &QListWidget::itemClicked, this, [this] { acceptCurrent(); });
}

void CompletionPopup::setItems(const QList<CompletionItem> &items)
{
    m_items = items;
    m_list->clear();
    for (const CompletionItem &item : items) {
        const QString text = item.detail.isEmpty() ? item.label
                                                    : item.label + QStringLiteral("  ") + item.detail;
        m_list->addItem(text);
    }
    if (!m_items.isEmpty())
        m_list->setCurrentRow(0);
}

void CompletionPopup::moveRow(int delta)
{
    if (m_list->count() == 0)
        return;
    const int row = qBound(0, m_list->currentRow() + delta, m_list->count() - 1);
    m_list->setCurrentRow(row);
}

void CompletionPopup::acceptCurrent()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_items.size())
        return;
    emit accepted(m_items.at(row));
}

bool CompletionPopup::isEmpty() const
{
    return m_items.isEmpty();
}
