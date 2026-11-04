#include "app/BottomPanel.h"

#include "app/OutputPanel.h"
#include "app/ProblemsPanel.h"
#include "app/TerminalPanel.h"

#include <QHBoxLayout>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

BottomPanel::BottomPanel(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumHeight(80);

    m_outputPage = new QToolButton(this);
    m_outputPage->setObjectName(QStringLiteral("outputPage"));
    m_outputPage->setText(QStringLiteral("Output"));
    m_outputPage->setCheckable(true);
    m_outputPage->setAutoRaise(true);
    m_problemsPage = new QToolButton(this);
    m_problemsPage->setObjectName(QStringLiteral("problemsPage"));
    m_problemsPage->setText(QStringLiteral("Problems"));
    m_problemsPage->setCheckable(true);
    m_problemsPage->setAutoRaise(true);
    m_terminalPage = new QToolButton(this);
    m_terminalPage->setObjectName(QStringLiteral("terminalPage"));
    m_terminalPage->setText(QStringLiteral("Terminal"));
    m_terminalPage->setCheckable(true);
    m_terminalPage->setAutoRaise(true);
    auto *closeButton = new QToolButton(this);
    closeButton->setText(QStringLiteral("\u2715"));
    closeButton->setToolTip(QStringLiteral("Close"));
    closeButton->setAutoRaise(true);

    m_output = new OutputPanel(this);
    m_problems = new ProblemsPanel(this);
    m_terminal = new TerminalPanel(this);
    m_stack = new QStackedWidget(this);
    m_stack->addWidget(m_output);
    m_stack->addWidget(m_problems);
    m_stack->addWidget(m_terminal);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(6, 4, 6, 4);
    header->setSpacing(4);
    header->addWidget(m_outputPage);
    header->addWidget(m_problemsPage);
    header->addWidget(m_terminalPage);
    header->addStretch(1);
    header->addWidget(closeButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(header);
    layout->addWidget(m_stack, 1);

    connect(m_outputPage, &QToolButton::clicked, this, &BottomPanel::presentOutput);
    connect(m_problemsPage, &QToolButton::clicked, this, &BottomPanel::showProblems);
    connect(m_terminalPage, &QToolButton::clicked, this, &BottomPanel::showTerminal);
    connect(m_problems, &ProblemsPanel::spansChanged, this, &BottomPanel::refreshProblemCount);
    connect(m_problems, &ProblemsPanel::showRequested, this, &BottomPanel::revealProblems);
    connect(closeButton, &QToolButton::clicked, this, &BottomPanel::dismiss);
    connect(m_output, &OutputPanel::showRequested, this, &BottomPanel::presentOutput);
    connect(m_output, &OutputPanel::keepVisible, this, &BottomPanel::showIfHidden);

    hide();
}

QSize BottomPanel::sizeHint() const
{
    return {800, 220};
}

void BottomPanel::setDirectoryProvider(std::function<QString()> provider)
{
    m_terminal->setDirectoryProvider(std::move(provider));
}

void BottomPanel::setColors(const QColor &foreground, const QColor &background, const QColor &selection,
                            const QList<QColor> &palette)
{
    m_terminal->setColors(foreground, background, selection, palette);
}

void BottomPanel::showTerminal()
{
    m_terminal->ensureSession();
    m_stack->setCurrentWidget(m_terminal);
    setPageChecked(m_terminalPage);
    const bool wasHidden = !isVisible();
    show();
    m_terminal->focusCurrent();
    emit terminalActive(true);
    if (wasHidden)
        emit opened();
}

void BottomPanel::hideTerminal()
{
    if (m_stack->currentWidget() == m_terminal)
        hide();
}

void BottomPanel::closeSessions()
{
    m_terminal->closeSessions();
}

void BottomPanel::presentOutput()
{
    m_stack->setCurrentWidget(m_output);
    setPageChecked(m_outputPage);
    const bool wasHidden = !isVisible();
    show();
    emit terminalActive(false);
    if (wasHidden)
        emit opened();
}

void BottomPanel::showProblems()
{
    m_stack->setCurrentWidget(m_problems);
    setPageChecked(m_problemsPage);
    const bool wasHidden = !isVisible();
    show();
    emit terminalActive(false);
    if (wasHidden)
        emit opened();
}

void BottomPanel::revealProblems()
{
    if (!isVisible())
        showProblems();
}

void BottomPanel::setProblemCount(int count)
{
    m_problemsPage->setText(count > 0 ? QStringLiteral("Problems (%1)").arg(count)
                                      : QStringLiteral("Problems"));
}

void BottomPanel::refreshProblemCount()
{
    setProblemCount(m_problems->spanCount());
}

void BottomPanel::showIfHidden()
{
    if (!isVisible())
        presentOutput();
}

void BottomPanel::dismiss()
{
    m_output->suppress();
    hide();
    emit terminalActive(false);
}

void BottomPanel::setPageChecked(QToolButton *active)
{
    m_outputPage->blockSignals(true);
    m_problemsPage->blockSignals(true);
    m_terminalPage->blockSignals(true);
    m_outputPage->setChecked(active == m_outputPage);
    m_problemsPage->setChecked(active == m_problemsPage);
    m_terminalPage->setChecked(active == m_terminalPage);
    m_outputPage->blockSignals(false);
    m_problemsPage->blockSignals(false);
    m_terminalPage->blockSignals(false);
}
