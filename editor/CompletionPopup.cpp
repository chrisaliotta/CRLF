#include "editor/CompletionPopup.h"

#include "syntax/Highlighter.h"
#include "syntax/LanguageDefinition.h"
#include "syntax/LanguageRegistry.h"
#include "syntax/Theme.h"

#include <QAbstractListModel>
#include <QAbstractTextDocumentLayout>
#include <QFrame>
#include <QItemSelectionModel>
#include <QListView>
#include <QPainter>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>
#include <QVBoxLayout>

namespace {

QString highlightedCode(const QString &code, const LanguageDefinition *definition,
                        const LanguageRegistry *registry, const Theme *theme)
{
    QTextDocument document;
    QFont font(QStringLiteral("Ubuntu Mono"));
    font.setPointSize(10);
    document.setDefaultFont(font);
    Highlighter highlighter(&document, definition, registry, theme);
    highlighter.setAutomatic(true);
    document.setPlainText(code);
    highlighter.rehighlight();
    // Formats live on the layout. Force one so the ranges are readable.
    (void)document.documentLayout()->documentSize();

    QString html;
    for (QTextBlock block = document.firstBlock(); block.isValid(); block = block.next()) {
        if (block.blockNumber() > 0)
            html += QLatin1String("<br>");
        const QString text = block.text();
        const QTextLayout *layout = block.layout();
        const auto formats = layout != nullptr ? layout->formats() : QList<QTextLayout::FormatRange>{};
        int cursor = 0;
        for (const QTextLayout::FormatRange &range : formats) {
            if (range.start > cursor)
                html += text.mid(cursor, range.start - cursor).toHtmlEscaped();
            const QString piece = text.mid(range.start, range.length).toHtmlEscaped();
            // A format with no brush still reports black. That black is what
            // made the signature disappear on the dark popup.
            const QBrush brush = range.format.foreground();
            if (brush.style() != Qt::NoBrush) {
                html += QStringLiteral("<span style=\"color:%1\">%2</span>")
                            .arg(brush.color().name(), piece);
            } else {
                html += piece;
            }
            cursor = range.start + range.length;
        }
        if (cursor < text.size())
            html += text.mid(cursor).toHtmlEscaped();
    }
    return html;
}

QString markdownFragment(const QString &markdown, const Theme *theme)
{
    if (markdown.trimmed().isEmpty())
        return {};
    QTextDocument document;
    const QString foreground = theme != nullptr ? theme->foreground.name() : QStringLiteral("#d4d4d4");
    document.setDefaultStyleSheet(QStringLiteral("body, p, span { color: %1; }").arg(foreground));
    document.setMarkdown(markdown);
    QString html = document.toHtml();
    html.replace(QLatin1String("color:#000000"), QStringLiteral("color:%1").arg(foreground));
    html.replace(QLatin1String("color:#000"), QStringLiteral("color:%1").arg(foreground));
    const int body = html.indexOf(QLatin1String("<body"));
    const int start = html.indexOf(QLatin1Char('>'), body);
    const int end = html.lastIndexOf(QLatin1String("</body>"));
    if (start < 0 || end < 0)
        return markdown.toHtmlEscaped();
    return html.mid(start + 1, end - start - 1);
}

QString renderDocumentation(const QString &markdown, const LanguageRegistry *registry, const Theme *theme,
                           const LanguageDefinition *fallback)
{
    QString html;
    int index = 0;
    while (index < markdown.size()) {
        const int fence = markdown.indexOf(QLatin1String("```"), index);
        if (fence < 0) {
            html += markdownFragment(markdown.mid(index), theme);
            break;
        }
        html += markdownFragment(markdown.mid(index, fence - index), theme);
        const int infoEnd = markdown.indexOf(QLatin1Char('\n'), fence + 3);
        if (infoEnd < 0)
            break;
        const QString info = markdown.mid(fence + 3, infoEnd - (fence + 3)).trimmed();
        const int close = markdown.indexOf(QLatin1String("```"), infoEnd + 1);
        if (close < 0)
            break;
        QString code = markdown.mid(infoEnd + 1, close - (infoEnd + 1));
        if (code.endsWith(QLatin1Char('\n')))
            code.chop(1);
        const LanguageDefinition *definition =
            registry != nullptr ? registry->definitionForFenceInfo(info) : nullptr;
        if (fallback != nullptr && (definition == nullptr || definition->name == QLatin1String("Plain Text")))
            definition = fallback;
        const QString background = theme != nullptr ? theme->codeBlockBackground.name() : QStringLiteral("#2b2b2f");
        const QString foreground = theme != nullptr ? theme->foreground.name() : QStringLiteral("#d4d4d4");
        // QLabel ignores div backgrounds. A table cell bgcolor is in the rich
        // text subset, so the sample sits in the same box as the list.
        html += QStringLiteral(
                    "<table width=\"100%\" bgcolor=\"%1\" cellspacing=\"0\" cellpadding=\"6\">"
                    "<tr><td style=\"color:%2; font-family:'Ubuntu Mono',monospace;\">%3</td></tr>"
                    "</table>")
                    .arg(background, foreground, highlightedCode(code, definition, registry, theme));
        index = close + 3;
    }
    return html;
}

const int kKindRole = Qt::UserRole;
const int kVisibleRows = 7;

QString rowText(const CompletionItem &item)
{
    return item.detail.isEmpty() ? item.label : item.label + QStringLiteral("  ") + item.detail;
}

} // namespace

class CompletionModel : public QAbstractListModel
{
public:
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_rows.size();
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (role == Qt::DisplayRole)
            return rowText(m_rows.at(index.row()));
        if (role == kKindRole)
            return m_rows.at(index.row()).kind;
        return {};
    }

    const QList<CompletionItem> &rows() const { return m_rows; }

    void setRows(const QList<CompletionItem> &rows)
    {
        beginResetModel();
        m_rows = rows;
        endResetModel();
    }

    void setRow(int row, const CompletionItem &item)
    {
        m_rows[row] = item;
        emit dataChanged(index(row), index(row));
    }

private:
    QList<CompletionItem> m_rows;
};

// The label and detail on the left, and what the row is on the right.
class CompletionRowDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void setForeground(const QColor &color) { m_foreground = color; }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem row = option;
        initStyleOption(&row, index);
        const QString text = row.text;
        // The style draws the row's background and selection. The text is
        // drawn here so the kind can share the line.
        row.text.clear();
        row.widget->style()->drawControl(QStyle::CE_ItemViewItem, &row, painter, row.widget);

        const QString kind = index.data(kKindRole).toString();
        const QRect area = row.rect.adjusted(8, 0, -8, 0);
        const int kindWidth = kind.isEmpty() ? 0 : row.fontMetrics.horizontalAdvance(kind) + 16;
        QColor dim = m_foreground;
        dim.setAlphaF(0.55f);
        painter->save();
        painter->setFont(row.font);
        painter->setPen(m_foreground);
        painter->drawText(area.adjusted(0, 0, -kindWidth, 0), Qt::AlignVCenter | Qt::AlignLeft,
                          row.fontMetrics.elidedText(text, Qt::ElideRight, area.width() - kindWidth));
        painter->setPen(dim);
        painter->drawText(area, Qt::AlignVCenter | Qt::AlignRight, kind);
        painter->restore();
    }

private:
    QColor m_foreground;
};

CompletionPopup::CompletionPopup(QWidget *parent)
    : QFrame(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_StyledBackground, true);
    m_model = new CompletionModel(this);
    m_delegate = new CompletionRowDelegate(this);
    m_list = new QListView(this);
    m_list->setModel(m_model);
    m_list->setItemDelegate(m_delegate);
    // Every row is one line, so the view measures one and never the rest.
    m_list->setUniformItemSizes(true);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setMinimumWidth(440);
    m_separator = new QFrame(this);
    m_separator->setObjectName(QStringLiteral("docSeparator"));
    m_separator->setFrameShape(QFrame::NoFrame);
    m_separator->setFixedHeight(1);
    m_separator->hide();
    m_documentation = new QTextBrowser(this);
    m_documentation->setOpenLinks(false);
    m_documentation->setOpenExternalLinks(false);
    m_documentation->setFrameShape(QFrame::NoFrame);
    m_documentation->setFocusPolicy(Qt::NoFocus);
    m_documentation->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_documentation->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_documentation->document()->setDocumentMargin(8);
    m_documentation->setMinimumWidth(440);
    m_documentation->hide();
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_list);
    layout->addWidget(m_separator);
    layout->addWidget(m_documentation);
    connect(m_list, &QListView::clicked, this, [this] { acceptCurrent(); });
    connect(m_list->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex &current) { showDocumentation(current.row()); });
    // Child widgets are visible with their parent. An empty list would cover
    // the top of every new editor until the first completion arrives.
    hide();
}

void CompletionPopup::setItems(const QList<CompletionItem> &items)
{
    m_all = items;
    m_prefix.clear();
    refill(QString());
}

void CompletionPopup::setPrefix(const QString &prefix)
{
    if (prefix == m_prefix)
        return;
    const QModelIndex current = m_list->currentIndex();
    const QString keep = current.isValid() ? m_model->rows().at(current.row()).label : QString();
    m_prefix = prefix;
    refill(keep);
}

void CompletionPopup::refill(const QString &keepLabel)
{
    QList<CompletionItem> rows;
    for (const CompletionItem &item : m_all) {
        const QString &match = item.filterText.isEmpty() ? item.label : item.filterText;
        if (match.startsWith(m_prefix, Qt::CaseInsensitive))
            rows.append(item);
    }
    // The row that was selected stays selected. Otherwise the one the server
    // expects, and otherwise the first.
    int select = -1;
    int expected = -1;
    for (int row = 0; row < rows.size() && select < 0; ++row) {
        if (!keepLabel.isEmpty() && rows.at(row).label == keepLabel)
            select = row;
        else if (expected < 0 && rows.at(row).preselect)
            expected = row;
    }
    if (select < 0)
        select = qMax(0, expected);
    {
        const QSignalBlocker blocker(m_list->selectionModel());
        m_model->setRows(rows);
        if (!rows.isEmpty()) {
            // The row height comes from the stylesheet, which a widget that
            // has never been shown has not read yet.
            m_list->ensurePolished();
            m_list->setFixedHeight(qMin(rows.size(), kVisibleRows) * m_list->sizeHintForRow(0)
                                   + 2 * m_list->frameWidth());
            m_list->setCurrentIndex(m_model->index(select));
            m_list->scrollTo(m_model->index(select), QAbstractItemView::PositionAtCenter);
        }
    }
    showDocumentation(rows.isEmpty() ? -1 : select);
}

void CompletionPopup::fillResolved(const QString &data, const QString &detail, const QString &documentation)
{
    const auto fill = [&](CompletionItem &item) {
        if (!detail.isEmpty())
            item.detail = detail;
        item.documentation = documentation;
    };
    for (CompletionItem &item : m_all) {
        if (item.data == data)
            fill(item);
    }
    for (int row = 0; row < m_model->rows().size(); ++row) {
        CompletionItem item = m_model->rows().at(row);
        if (item.data != data)
            continue;
        fill(item);
        m_model->setRow(row, item);
        if (row == m_list->currentIndex().row())
            showDocumentation(row);
    }
}

void CompletionPopup::moveRow(int delta)
{
    const int row = qBound(0, m_list->currentIndex().row() + delta, m_model->rowCount() - 1);
    m_list->setCurrentIndex(m_model->index(row));
}

void CompletionPopup::acceptCurrent()
{
    emit accepted(m_model->rows().at(m_list->currentIndex().row()));
}

bool CompletionPopup::isEmpty() const
{
    return m_model->rows().isEmpty();
}

void CompletionPopup::setHighlighting(const LanguageRegistry *registry, const Theme *theme,
                                      const LanguageDefinition *language)
{
    m_registry = registry;
    m_theme = theme;
    m_language = language;
    if (theme == nullptr)
        return;
    const QString background = theme->completionBackground.name();
    const QString foreground = theme->completionForeground.name();
    const QString border = theme->completionBorder.name();
    const QString selection = theme->selection.name();
    // No color rule on the documentation view. A stylesheet color replaces
    // every span, which is why the signature had no token colors.
    setStyleSheet(QStringLiteral(
        "CompletionPopup { background: %1; border: 1px solid %2; }"
        "CompletionPopup QListView { background: %1; color: %3; border: none; outline: 0; }"
        "CompletionPopup QListView::item { padding: 2px 8px; }"
        "CompletionPopup QListView::item:selected { background: %4; color: %3; }"
        "CompletionPopup QTextBrowser { background: %1; border: none; }"
        "CompletionPopup QFrame#docSeparator { background: %2; border: none; }")
                      .arg(background, border, foreground, selection));
    m_delegate->setForeground(theme->completionForeground);
    QPalette pal = m_documentation->palette();
    pal.setColor(QPalette::Base, theme->completionBackground);
    pal.setColor(QPalette::Text, theme->completionForeground);
    m_documentation->setPalette(pal);
    m_documentation->document()->setDefaultStyleSheet(
        QStringLiteral("body, p { color: %1; }").arg(foreground));
    const int row = m_list->currentIndex().row();
    if (row >= 0)
        showDocumentation(row);
}

void CompletionPopup::showDocumentation(int row)
{
    const bool selected = row >= 0;
    const QString text = selected ? m_model->rows().at(row).documentation : QString();
    if (text.isEmpty()) {
        m_separator->hide();
        m_documentation->hide();
        adjustSize();
        if (selected)
            emit documentationRequested(m_model->rows().at(row));
        return;
    }
    m_documentation->setHtml(renderDocumentation(text, m_registry, m_theme, m_language));
    m_documentation->document()->setTextWidth(m_documentation->minimumWidth() - 2);
    const int height = qBound(36, int(m_documentation->document()->size().height()) + 2, 180);
    m_documentation->setFixedHeight(height);
    m_separator->show();
    m_documentation->show();
    adjustSize();
}
