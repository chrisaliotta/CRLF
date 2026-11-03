#include "app/ProblemsPanel.h"

#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

enum Role {
    KeyRole = Qt::UserRole,
    PathRole,
    EditorRole,
    LineRole,
    ColumnRole,
    SeverityRole,
    MessageRole
};

} // namespace

ProblemsPanel::ProblemsPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ProblemsPanel"));
    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("problemsTree"));
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({QStringLiteral("Severity"), QStringLiteral("Location"), QStringLiteral("Message")});
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setStretchLastSection(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        if (item == nullptr || item->data(0, LineRole).isNull())
            return;
        emit diagnosticActivated(item->data(0, PathRole).toString(),
                                 quintptr(item->data(0, EditorRole).toULongLong()),
                                 item->data(0, LineRole).toInt(), item->data(0, ColumnRole).toInt());
    });
}

void ProblemsPanel::setProjectDirectory(const QString &directory)
{
    m_projectDirectory = directory;
}

QString ProblemsPanel::directoryFor(const QString &path) const
{
    if (path.isEmpty())
        return QStringLiteral("(unsaved)");
    if (!m_projectDirectory.isEmpty()) {
        const QString relative = QDir(m_projectDirectory).relativeFilePath(path);
        if (!relative.startsWith(QLatin1String(".."))) {
            const QString directory = QFileInfo(relative).path();
            return directory == QLatin1String(".") ? QStringLiteral(".") : directory;
        }
    }
    return QFileInfo(path).absolutePath();
}

QString ProblemsPanel::fileNameFor(const QString &path, const QString &title) const
{
    if (path.isEmpty())
        return title;
    return QFileInfo(path).fileName();
}

QTreeWidgetItem *ProblemsPanel::directoryItem(const QString &directory)
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        if (item->text(0) == directory)
            return item;
        if (QString::compare(item->text(0), directory, Qt::CaseInsensitive) > 0) {
            auto *created = new QTreeWidgetItem(QStringList{directory});
            m_tree->insertTopLevelItem(i, created);
            created->setFirstColumnSpanned(true);
            created->setExpanded(true);
            return created;
        }
    }
    auto *created = new QTreeWidgetItem(QStringList{directory});
    m_tree->addTopLevelItem(created);
    created->setFirstColumnSpanned(true);
    created->setExpanded(true);
    return created;
}

QTreeWidgetItem *ProblemsPanel::fileItem(const QString &key) const
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *directory = m_tree->topLevelItem(i);
        for (int j = 0; j < directory->childCount(); ++j) {
            QTreeWidgetItem *file = directory->child(j);
            if (file->data(0, KeyRole).toString() == key)
                return file;
        }
    }
    return nullptr;
}

QList<Diagnostic> ProblemsPanel::spansOf(QTreeWidgetItem *file) const
{
    QList<Diagnostic> spans;
    for (int i = 0; i < file->childCount(); ++i) {
        QTreeWidgetItem *row = file->child(i);
        Diagnostic span;
        span.line = row->data(0, LineRole).toInt();
        span.column = row->data(0, ColumnRole).toInt();
        span.severity = row->data(0, SeverityRole).toString();
        span.message = row->data(0, MessageRole).toString();
        spans.append(span);
    }
    return spans;
}

void ProblemsPanel::removeItem(QTreeWidgetItem *file)
{
    QTreeWidgetItem *directory = file->parent();
    directory->removeChild(file);
    delete file;
    if (directory->childCount() == 0) {
        const int index = m_tree->indexOfTopLevelItem(directory);
        delete m_tree->takeTopLevelItem(index);
    }
}

void ProblemsPanel::removeFile(const QString &key)
{
    QTreeWidgetItem *file = fileItem(key);
    if (!file)
        return;
    removeItem(file);
    emit spansChanged();
}

void ProblemsPanel::clear()
{
    if (m_tree->topLevelItemCount() == 0)
        return;
    m_tree->clear();
    emit spansChanged();
}

void ProblemsPanel::setFile(const QString &key, const QString &path, const QString &title, quintptr editorId,
                            const QList<Diagnostic> &spans, bool reveal)
{
    removeFile(key);
    if (spans.isEmpty())
        return;

    const QString directory = directoryFor(path);
    const QString name = fileNameFor(path, title);
    QTreeWidgetItem *parent = directoryItem(directory);
    int insertAt = 0;
    for (; insertAt < parent->childCount(); ++insertAt) {
        if (QString::compare(parent->child(insertAt)->text(0), name, Qt::CaseInsensitive) > 0)
            break;
    }
    auto *file = new QTreeWidgetItem;
    file->setText(0, name);
    file->setData(0, KeyRole, key);
    file->setFirstColumnSpanned(true);
    file->setExpanded(true);
    parent->insertChild(insertAt, file);

    for (const Diagnostic &span : spans) {
        auto *row = new QTreeWidgetItem;
        row->setText(0, span.warning() ? QStringLiteral("warning") : QStringLiteral("error"));
        row->setText(1, QStringLiteral("%1:%2").arg(span.line).arg(span.column));
        row->setText(2, span.message);
        row->setData(0, PathRole, path);
        row->setData(0, EditorRole, qulonglong(editorId));
        row->setData(0, LineRole, span.line);
        row->setData(0, ColumnRole, span.column);
        row->setData(0, SeverityRole, span.severity);
        row->setData(0, MessageRole, span.message);
        file->addChild(row);
    }
    emit spansChanged();
    if (reveal)
        emit showRequested();
}

void ProblemsPanel::retarget(const QString &oldKey, const QString &newKey, const QString &path, const QString &title,
                             quintptr editorId)
{
    QTreeWidgetItem *file = fileItem(oldKey);
    if (!file || oldKey == newKey)
        return;
    const QList<Diagnostic> spans = spansOf(file);
    removeFile(oldKey);
    setFile(newKey, path, title, editorId, spans, false);
}

int ProblemsPanel::spanCount() const
{
    int count = 0;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *directory = m_tree->topLevelItem(i);
        for (int j = 0; j < directory->childCount(); ++j)
            count += directory->child(j)->childCount();
    }
    return count;
}
