#include "app/TemplateDialog.h"

#include "app/FileIconTheme.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSize>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {

struct CommandKey
{
    const char *key;
    const char *meaning;
};

const CommandKey kCommandKeys[] = {
    {"file", "Full path of the focused document"},
    {"fileName", "File name and extension. An unsaved document uses the tab title"},
    {"fileBase", "File name without the extension"},
    {"fileExt", "Extension, without the leading dot"},
    {"fileDir", "Directory that contains the file"},
    {"relativeFile", "Path of the file relative to ${project}"},
    {"line", "Cursor line, starting at 1"},
    {"column", "Cursor column, starting at 1"},
    {"selection", "The selected text"},
    {"language", "Language of the focused document"},
    {"project", "Directory that contains the .crlf file"},
    {"folder", "Folder the explorer is scoped to"},
    {"cwd", "This command's working directory"},
};

QListWidget *filteredList(QLineEdit *filter, QWidget *parent)
{
    auto *list = new QListWidget(parent);
    QObject::connect(filter, &QLineEdit::textChanged, list, [list](const QString &text) {
        for (int i = 0; i < list->count(); ++i) {
            QListWidgetItem *item = list->item(i);
            item->setHidden(!item->text().contains(text, Qt::CaseInsensitive)
                            && !item->toolTip().contains(text, Qt::CaseInsensitive));
        }
    });
    return list;
}

void copyKey(QListWidgetItem *item)
{
    const QString key = item->data(Qt::UserRole).toString();
    QApplication::clipboard()->setText(key);
}

} // namespace

TemplateDialog::TemplateDialog(const FileIconTheme *icons, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Templates"));
    resize(520, 480);

    auto *hint = new QLabel(
        QStringLiteral("Keys for a .crlf file. Double-click a row to copy it."), this);

    auto *iconFilter = new QLineEdit(this);
    iconFilter->setPlaceholderText(QStringLiteral("Filter icon keys"));
    auto *iconList = filteredList(iconFilter, this);
    iconList->setIconSize(QSize(16, 16));
    if (icons != nullptr) {
        for (const IconOption &row : icons->icons()) {
            const QString label = row.key + QStringLiteral("    ") + row.kind;
            auto *item = new QListWidgetItem(row.icon, label, iconList);
            item->setData(Qt::UserRole, row.key);
            item->setToolTip(row.kind);
        }
    }
    connect(iconList, &QListWidget::itemDoubleClicked, this, [](QListWidgetItem *item) { copyKey(item); });

    auto *iconsPage = new QWidget(this);
    auto *iconsLayout = new QVBoxLayout(iconsPage);
    iconsLayout->setContentsMargins(0, 8, 0, 0);
    iconsLayout->addWidget(iconFilter);
    iconsLayout->addWidget(iconList, 1);

    auto *commandFilter = new QLineEdit(this);
    commandFilter->setPlaceholderText(QStringLiteral("Filter command keys"));
    auto *commandList = filteredList(commandFilter, this);
    for (const CommandKey &entry : kCommandKeys) {
        const QString token = QStringLiteral("${%1}").arg(QLatin1String(entry.key));
        auto *item = new QListWidgetItem(token + QStringLiteral("    ") + QLatin1String(entry.meaning), commandList);
        item->setData(Qt::UserRole, token);
        item->setToolTip(QLatin1String(entry.meaning));
    }
    connect(commandList, &QListWidget::itemDoubleClicked, this, [](QListWidgetItem *item) { copyKey(item); });

    auto *commandsPage = new QWidget(this);
    auto *commandsLayout = new QVBoxLayout(commandsPage);
    commandsLayout->setContentsMargins(0, 8, 0, 0);
    commandsLayout->addWidget(commandFilter);
    commandsLayout->addWidget(commandList, 1);

    auto *tabs = new QTabWidget(this);
    tabs->addTab(iconsPage, QStringLiteral("Icons"));
    tabs->addTab(commandsPage, QStringLiteral("Command keys"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(hint);
    layout->addWidget(tabs, 1);
    layout->addWidget(buttons);
}
