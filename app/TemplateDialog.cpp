#include "app/TemplateDialog.h"

#include "app/FileIconTheme.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QPushButton>
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
    {"buffer", "Path of a file holding the focused editor's text, including unsaved edits"},
    {"terminal", "Path of a file holding the current terminal's plain text"},
    {"language", "Language of the focused document"},
    {"project", "Project root: the folder that holds the .crlf files"},
    {"folder", "Folder the explorer is scoped to"},
    {"cwd", "This command's working directory"},
    {"programDir", "Folder beside the program that holds styles, themes, and scripts"},
    {"appDir", "The application's .crlf folder: scripts, keys, and rules shared by every workspace"},
};

void hideUnless(QListWidget *list, const QString &text)
{
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem *item = list->item(i);
        const bool match = text.isEmpty() || item->text().contains(text, Qt::CaseInsensitive)
                           || item->toolTip().contains(text, Qt::CaseInsensitive);
        item->setHidden(!match);
    }
}

QString mappingLabel(const IconRule &rule)
{
    QString label = rule.kind + QStringLiteral("    ") + rule.pattern + QStringLiteral("    ")
                    + rule.from + QStringLiteral(" / ") + rule.icon;
    if (!rule.iconOpen.isEmpty())
        label += QStringLiteral("    open ") + rule.iconOpen;
    return label;
}

} // namespace

TemplateDialog::TemplateDialog(const QList<FileIconTheme> &themes, const ProjectIcons &icons,
                               const QString &fallbackTheme, QWidget *parent)
    : QDialog(parent)
    , m_themes(themes)
    , m_icons(icons)
    , m_fallbackTheme(fallbackTheme)
{
    setWindowTitle(QStringLiteral("Templates"));
    resize(720, 560);

    for (const FileIconTheme &theme : m_themes) {
        for (const IconOption &row : theme.icons())
            m_catalog.append({theme.name, row.key, row.kind, row.icon});
        for (const IconOption &row : theme.glyphs())
            m_glyphs.append({theme.name, row.key, row.kind, row.icon});
    }

    auto *filter = new QLineEdit(this);
    filter->setPlaceholderText(QStringLiteral("Search"));
    auto *catalog = new QListWidget(this);
    catalog->setIconSize(QSize(16, 16));
    for (const CatalogIcon &row : m_catalog) {
        const QString label = row.key + QStringLiteral("    ") + row.kind + QStringLiteral("    ") + row.theme;
        auto *item = new QListWidgetItem(row.icon, label, catalog);
        item->setData(Qt::UserRole, row.key);
        item->setToolTip(row.kind);
    }
    for (const CommandKey &entry : kCommandKeys) {
        const QString token = QStringLiteral("${%1}").arg(QLatin1String(entry.key));
        auto *item = new QListWidgetItem(token + QStringLiteral("    ") + QLatin1String(entry.meaning), catalog);
        item->setData(Qt::UserRole, token);
        item->setToolTip(QStringLiteral("command"));
    }
    connect(filter, &QLineEdit::textChanged, catalog, [catalog](const QString &text) { hideUnless(catalog, text); });
    connect(catalog, &QListWidget::itemDoubleClicked, this, [](QListWidgetItem *item) {
        QApplication::clipboard()->setText(item->data(Qt::UserRole).toString());
    });

    auto *rulesPage = new QWidget(this);
    auto *rulesLayout = new QVBoxLayout(rulesPage);
    rulesLayout->setContentsMargins(0, 8, 0, 0);
    rulesLayout->addWidget(filter);
    rulesLayout->addWidget(catalog, 1);

    m_theme = new QComboBox(this);
    m_theme->addItem(QStringLiteral("Use File Icons menu"), QString());
    int themeIndex = 0;
    for (int i = 0; i < m_themes.size(); ++i) {
        m_theme->addItem(m_themes.at(i).name, m_themes.at(i).name);
        if (m_themes.at(i).name == m_icons.theme)
            themeIndex = i + 1;
    }
    m_theme->setCurrentIndex(themeIndex);

    m_kind = new QComboBox(this);
    m_kind->addItem(QStringLiteral("extension"), QStringLiteral("extension"));
    m_kind->addItem(QStringLiteral("file"), QStringLiteral("file"));
    m_kind->addItem(QStringLiteral("folder"), QStringLiteral("folder"));

    m_expression = new QLineEdit(this);
    m_expression->setPlaceholderText(QStringLiteral("Name, or a pattern with * or ?"));

    m_iconTheme = new QComboBox(this);
    for (const FileIconTheme &theme : m_themes)
        m_iconTheme->addItem(theme.name, theme.name);
    const int sourceIndex = m_iconTheme->findData(m_fallbackTheme);
    if (sourceIndex >= 0)
        m_iconTheme->setCurrentIndex(sourceIndex);

    m_iconKey = new QListWidget(this);
    m_openKey = new QListWidget(this);
    m_iconKey->setViewMode(QListView::IconMode);
    m_openKey->setViewMode(QListView::IconMode);
    m_iconKey->setIconSize(QSize(32, 32));
    m_openKey->setIconSize(QSize(32, 32));
    m_iconKey->setResizeMode(QListView::Adjust);
    m_openKey->setResizeMode(QListView::Adjust);
    m_iconKey->setMovement(QListView::Static);
    m_openKey->setMovement(QListView::Static);
    m_iconKey->setSpacing(6);
    m_openKey->setSpacing(6);
    m_iconKey->setGridSize(QSize(44, 44));
    m_openKey->setGridSize(QSize(44, 44));
    m_iconKey->setMinimumHeight(180);
    m_openKey->setMinimumHeight(180);
    m_iconSearch = new QLineEdit(this);
    m_iconSearch->setPlaceholderText(QStringLiteral("Search icons"));
    fillIconKeys();
    connect(m_iconTheme, &QComboBox::currentIndexChanged, this, [this] { fillIconKeys(); });
    connect(m_iconSearch, &QLineEdit::textChanged, this, [this](const QString &text) {
        hideUnless(m_iconKey, text);
        hideUnless(m_openKey, text);
    });

    m_openRow = new QWidget(this);
    auto *openLayout = new QVBoxLayout(m_openRow);
    openLayout->setContentsMargins(0, 0, 0, 0);
    openLayout->addWidget(new QLabel(QStringLiteral("Open icon"), m_openRow));
    openLayout->addWidget(m_openKey, 1);
    m_openRow->setVisible(false);
    connect(m_kind, &QComboBox::currentIndexChanged, this, [this] {
        m_openRow->setVisible(m_kind->currentData().toString() == QLatin1String("folder"));
    });

    auto *add = new QPushButton(QStringLiteral("Add"), this);
    auto *remove = new QPushButton(QStringLiteral("Remove"), this);
    connect(add, &QPushButton::clicked, this, &TemplateDialog::addMapping);
    connect(remove, &QPushButton::clicked, this, &TemplateDialog::removeMapping);

    m_mappings = new QListWidget(this);
    m_mappings->setIconSize(QSize(16, 16));
    refreshMappings();

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Base theme"), m_theme);
    form->addRow(QStringLiteral("Type"), m_kind);
    form->addRow(QStringLiteral("Expression"), m_expression);
    form->addRow(QStringLiteral("Icon theme"), m_iconTheme);
    form->addRow(QStringLiteral("Search"), m_iconSearch);

    auto *pickers = new QHBoxLayout;
    auto *closedColumn = new QVBoxLayout;
    closedColumn->addWidget(new QLabel(QStringLiteral("Icon"), this));
    closedColumn->addWidget(m_iconKey, 1);
    pickers->addLayout(closedColumn, 1);
    pickers->addWidget(m_openRow, 1);

    auto *mapPage = new QWidget(this);
    auto *mapLayout = new QVBoxLayout(mapPage);
    mapLayout->setContentsMargins(0, 8, 0, 0);
    mapLayout->addLayout(form);
    mapLayout->addLayout(pickers, 1);
    auto *actions = new QHBoxLayout;
    actions->addWidget(add);
    actions->addWidget(remove);
    actions->addStretch(1);
    mapLayout->addLayout(actions);
    mapLayout->addWidget(m_mappings, 1);

    auto *tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("templateTabs"));
    tabs->setDocumentMode(false);
    tabs->tabBar()->setExpanding(false);
    tabs->setStyleSheet(QStringLiteral(
        "#templateTabs::pane { border: 1px solid palette(mid); top: -1px; }"
        "#templateTabs QTabBar::tab {"
        "  background: palette(button); color: palette(button-text);"
        "  padding: 8px 22px; margin: 0 4px 0 0;"
        "  border: 1px solid palette(mid); border-bottom: none;"
        "  border-top-left-radius: 6px; border-top-right-radius: 6px; }"
        "#templateTabs QTabBar::tab:selected { background: palette(base); font-weight: 600; }"
        "#templateTabs QTabBar::tab:hover:!selected { background: palette(midlight); }"));
    tabs->addTab(rulesPage, QStringLiteral("Rules"));
    tabs->addTab(mapPage, QStringLiteral("Mappings"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        m_icons.theme = m_theme->currentData().toString();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *hint = new QLabel(
        QStringLiteral("Rules lists every icon key. Mappings adds a file, extension, or folder association. Double-click a rule to copy its key."),
        this);
    hint->setWordWrap(true);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(hint);
    layout->addWidget(tabs, 1);
    layout->addWidget(buttons);
}

void TemplateDialog::fillIconKeys()
{
    const QString theme = m_iconTheme->currentData().toString();
    const auto fill = [&](QListWidget *list, bool optional) {
        list->clear();
        if (optional) {
            auto *same = new QListWidgetItem(QStringLiteral("Same"), list);
            same->setData(Qt::UserRole, QString());
            same->setToolTip(QStringLiteral("Use the closed icon"));
        }
        for (const CatalogIcon &row : m_glyphs) {
            if (row.theme != theme)
                continue;
            auto *item = new QListWidgetItem(row.icon, QString(), list);
            item->setData(Qt::UserRole, row.key);
            item->setToolTip(row.key);
        }
    };
    fill(m_iconKey, false);
    fill(m_openKey, true);
    hideUnless(m_iconKey, m_iconSearch->text());
    hideUnless(m_openKey, m_iconSearch->text());
}

void TemplateDialog::refreshMappings()
{
    m_mappings->clear();
    for (const IconRule &rule : m_icons.rules) {
        QIcon icon;
        for (const CatalogIcon &row : m_catalog) {
            if (row.theme == rule.from && row.key.compare(rule.icon, Qt::CaseInsensitive) == 0) {
                icon = row.icon;
                break;
            }
        }
        m_mappings->addItem(new QListWidgetItem(icon, mappingLabel(rule), m_mappings));
    }
}

void TemplateDialog::addMapping()
{
    const QString expression = m_expression->text().trimmed();
    const QListWidgetItem *chosen = m_iconKey->currentItem();
    const QString icon = chosen == nullptr ? QString() : chosen->data(Qt::UserRole).toString();
    if (expression.isEmpty() || icon.isEmpty())
        return;
    IconRule rule;
    rule.kind = m_kind->currentData().toString();
    rule.pattern = expression;
    rule.from = m_iconTheme->currentData().toString();
    rule.icon = icon;
    if (rule.kind == QLatin1String("folder") && m_openKey->currentItem() != nullptr)
        rule.iconOpen = m_openKey->currentItem()->data(Qt::UserRole).toString();
    m_icons.rules.append(rule);
    m_expression->clear();
    refreshMappings();
}

void TemplateDialog::removeMapping()
{
    const int row = m_mappings->currentRow();
    if (row < 0 || row >= m_icons.rules.size())
        return;
    m_icons.rules.removeAt(row);
    refreshMappings();
}
