#include "app/TerminalPanel.h"

#include "app/ConPtySession.h"
#include "app/TerminalView.h"
#include "io/WslPath.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

const QString kWsl = QStringLiteral("C:/Windows/System32/wsl.exe");
const QString kPowerShell = QStringLiteral("C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe");

} // namespace

TerminalPanel::TerminalPanel(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);

    m_sessionsCombo = new QComboBox(this);
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
    header->setContentsMargins(6, 4, 6, 4);
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

void TerminalPanel::setColors(const QColor &foreground, const QColor &background, const QColor &selection)
{
    m_foreground = foreground;
    m_background = background;
    m_selection = selection;
    for (const Session &session : m_sessions)
        session.view->setColors(foreground, background, selection);
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
    view->setColors(m_foreground, m_background, m_selection);
    auto *pty = new ConPtySession(this);
    connect(pty, &ConPtySession::readyRead, view, &TerminalView::pushBytes);
    connect(view, &TerminalView::inputGenerated, pty, &ConPtySession::writeInput);
    connect(view, &TerminalView::resized, pty, &ConPtySession::resize);

    m_sessions.append({pty, view});
    m_stack->addWidget(view);
    m_sessionsCombo->addItem(uniqueLabel(launch.label, -1));
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
    m_sessionsCombo->setItemText(index, uniqueLabel(launch.label, index));
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
