#include "app/TerminalPanel.h"

#include "app/ConPtySession.h"
#include "app/TerminalView.h"
#include "io/WslPath.h"

#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

const QString kWsl = QStringLiteral("C:/Windows/System32/wsl.exe");
const QString kPowerShell = QStringLiteral("C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe");

constexpr int kMarginX = 6;
constexpr int kMarginY = 4;
constexpr int kTerminalPaddingX = 12;
constexpr int kTerminalPaddingY = 8;

} // namespace

TerminalPanel::TerminalPanel(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);

    m_sessionsCombo = new QComboBox(this);
    m_sessionsCombo->setEditable(true);
    m_sessionsCombo->setInsertPolicy(QComboBox::NoInsert);
    m_sessionsCombo->lineEdit()->setReadOnly(true);
    m_sessionsCombo->lineEdit()->setCursor(Qt::ArrowCursor);
    m_sessionsCombo->lineEdit()->installEventFilter(this);
    m_openList = new QTimer(this);
    m_openList->setSingleShot(true);
    m_openList->setInterval(QApplication::doubleClickInterval());
    connect(m_openList, &QTimer::timeout, m_sessionsCombo, &QComboBox::showPopup);
    auto *newSession = new QToolButton(this);
    newSession->setText(QStringLiteral("+"));
    newSession->setToolTip(QStringLiteral("New terminal"));
    newSession->setAutoRaise(true);
    m_restart = new QToolButton(this);
    m_restart->setText(QStringLiteral("Restart"));
    m_restart->setToolTip(QStringLiteral("Restart"));
    m_restart->setAutoRaise(true);
    m_closeSession = new QToolButton(this);
    m_closeSession->setText(QStringLiteral("Close Session"));
    m_closeSession->setToolTip(QStringLiteral("Close session"));
    m_closeSession->setAutoRaise(true);

    m_stack = new QStackedWidget(this);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(kMarginX, kMarginY, kMarginX, kMarginY);
    header->setSpacing(8);
    header->addWidget(m_sessionsCombo, 1);
    header->addWidget(newSession);
    header->addWidget(m_restart);
    header->addWidget(m_closeSession);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(header);
    layout->addWidget(m_stack, 1);

    connect(m_sessionsCombo, &QComboBox::currentIndexChanged, this, &TerminalPanel::showSession);
    connect(newSession, &QToolButton::clicked, this, &TerminalPanel::openSession);
    connect(m_restart, &QToolButton::clicked, this, &TerminalPanel::restartCurrent);
    connect(m_closeSession, &QToolButton::clicked, this, &TerminalPanel::closeCurrent);
    updateButtons();
}

void TerminalPanel::setDirectoryProvider(std::function<QString()> provider)
{
    m_directory = std::move(provider);
}

void TerminalPanel::setColors(const QColor &foreground, const QColor &background, const QColor &selection,
                              const QList<QColor> &palette)
{
    m_foreground = foreground;
    m_background = background;
    m_selection = selection;
    m_palette = palette;
    for (const Session &session : m_sessions)
        session.view->setColors(foreground, background, selection, palette);
}

void TerminalPanel::ensureSession()
{
    if (m_sessions.isEmpty())
        openSession();
}

void TerminalPanel::focusCurrent()
{
    const int index = m_sessionsCombo->currentIndex();
    if (index >= 0 && index < m_sessions.size())
        m_sessions.at(index).view->setFocus();
}

void TerminalPanel::closeSessions()
{
    for (const Session &session : m_sessions)
        session.pty->stop();
}

bool TerminalPanel::eventFilter(QObject *, QEvent *event)
{
    QLineEdit *name = m_sessionsCombo->lineEdit();
    if (name->isReadOnly()) {
        if (event->type() == QEvent::MouseButtonPress) {
            m_openList->start();
            return true;
        }
        if (event->type() == QEvent::MouseButtonDblClick) {
            m_openList->stop();
            beginRename();
            return true;
        }
        return false;
    }
    if (event->type() == QEvent::FocusOut) {
        endRename(true);
        return false;
    }
    if (event->type() != QEvent::KeyPress)
        return false;
    const int key = static_cast<QKeyEvent *>(event)->key();
    if (key != Qt::Key_Return && key != Qt::Key_Enter && key != Qt::Key_Escape)
        return false;
    endRename(key != Qt::Key_Escape);
    focusCurrent();
    return true;
}

QString TerminalPanel::nameFor(int index) const
{
    const Session &session = m_sessions.at(index);
    return session.alias.isEmpty() ? uniqueLabel(session.label, index) : session.alias;
}

void TerminalPanel::beginRename()
{
    QLineEdit *name = m_sessionsCombo->lineEdit();
    name->setReadOnly(false);
    name->setCursor(Qt::IBeamCursor);
    name->setFocus();
    name->selectAll();
}

void TerminalPanel::endRename(bool commit)
{
    QLineEdit *name = m_sessionsCombo->lineEdit();
    name->setReadOnly(true);
    name->setCursor(Qt::ArrowCursor);
    const int index = m_sessionsCombo->currentIndex();
    if (commit) {
        m_sessions[index].alias = name->text().trimmed();
        m_sessionsCombo->setItemText(index, nameFor(index));
    }
    name->setText(m_sessionsCombo->itemText(index));
}

QString TerminalPanel::transcript() const
{
    const int index = m_sessionsCombo->currentIndex();
    if (index < 0 || index >= m_sessions.size())
        return {};
    const TerminalView *view = m_sessions.at(index).view;
    return view != nullptr ? view->transcript() : QString();
}

TerminalPanel::Launch TerminalPanel::launchFor(const QString &directory) const
{
    const WslPath::LinuxPath linux = WslPath::toLinux(directory);
    if (linux.valid()) {
        return {kWsl,
                {QStringLiteral("-d"), linux.distro, QStringLiteral("--cd"), linux.path},
                QString(),
                linux.distro};
    }
    return {kPowerShell,
            {QStringLiteral("-NoLogo"), QStringLiteral("-NoExit")},
            directory,
            QStringLiteral("PowerShell")};
}

QString TerminalPanel::uniqueLabel(const QString &base, int ignoreIndex) const
{
    const auto taken = [this, ignoreIndex](const QString &label) {
        for (int i = 0; i < m_sessionsCombo->count(); ++i) {
            if (i != ignoreIndex && m_sessionsCombo->itemText(i) == label)
                return true;
        }
        return false;
    };
    if (!taken(base))
        return base;
    for (int number = 2;; ++number) {
        const QString candidate = base + QLatin1Char(' ') + QString::number(number);
        if (!taken(candidate))
            return candidate;
    }
}

void TerminalPanel::openSession()
{
    const Launch launch = launchFor(m_directory ? m_directory() : QString());
    auto *view = new TerminalView;
    view->setContentsMargins(kTerminalPaddingX, kTerminalPaddingY, kTerminalPaddingX, kTerminalPaddingY);
    view->setColors(m_foreground, m_background, m_selection, m_palette);
    auto *pty = new ConPtySession(this);
    connect(pty, &ConPtySession::readyRead, view, &TerminalView::pushBytes);
    connect(view, &TerminalView::inputGenerated, pty, &ConPtySession::writeInput);
    connect(view, &TerminalView::resized, pty, &ConPtySession::resize);

    m_sessions.append({pty, view, launch.label, QString()});
    m_stack->addWidget(view);
    m_sessionsCombo->addItem(nameFor(m_sessions.size() - 1));
    m_sessionsCombo->setCurrentIndex(m_sessionsCombo->count() - 1);
    updateButtons();

    QTimer::singleShot(0, view, [pty, view, launch] {
        view->syncSize();
        pty->start(launch.program, launch.arguments, launch.workingDirectory, view->rows(), view->cols());
        view->setFocus();
    });
}

void TerminalPanel::restartCurrent()
{
    const int index = m_sessionsCombo->currentIndex();
    if (index < 0 || index >= m_sessions.size())
        return;
    Session &session = m_sessions[index];
    session.pty->stop();
    session.view->reset();
    const Launch launch = launchFor(m_directory ? m_directory() : QString());
    session.label = launch.label;
    m_sessionsCombo->setItemText(index, nameFor(index));
    session.view->syncSize();
    session.pty->start(launch.program, launch.arguments, launch.workingDirectory, session.view->rows(),
                       session.view->cols());
    session.view->setFocus();
}

void TerminalPanel::closeCurrent()
{
    const int index = m_sessionsCombo->currentIndex();
    if (index < 0 || index >= m_sessions.size())
        return;
    Session session = m_sessions.takeAt(index);
    session.pty->stop();
    m_sessionsCombo->blockSignals(true);
    m_sessionsCombo->removeItem(index);
    m_sessionsCombo->blockSignals(false);
    m_stack->removeWidget(session.view);
    session.view->deleteLater();
    session.pty->deleteLater();
    if (m_sessionsCombo->currentIndex() >= 0)
        showSession(m_sessionsCombo->currentIndex());
    updateButtons();
}

void TerminalPanel::showSession(int index)
{
    if (index < 0 || index >= m_sessions.size())
        return;
    m_stack->setCurrentWidget(m_sessions.at(index).view);
    m_sessions.at(index).view->syncSize();
    m_sessions.at(index).view->setFocus();
}

void TerminalPanel::updateButtons()
{
    const bool any = !m_sessions.isEmpty();
    m_restart->setEnabled(any);
    m_closeSession->setEnabled(any);
    m_sessionsCombo->setEnabled(any);
}
