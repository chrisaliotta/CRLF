#include "app/OutputPanel.h"

#include <QHBoxLayout>
#include <QComboBox>
#include <QFont>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

OutputPanel::OutputPanel(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(220);

    m_commands = new QComboBox(this);
    m_status = new QLabel(this);
    m_stop = new QPushButton(QStringLiteral("Stop"), this);
    m_stop->hide();
    auto *clearButton = new QToolButton(this);
    clearButton->setText(QStringLiteral("Clear"));
    clearButton->setToolTip(QStringLiteral("Clear output"));
    clearButton->setAutoRaise(true);
    auto *closeButton = new QToolButton(this);
    closeButton->setText(QStringLiteral("\u2715"));
    closeButton->setToolTip(QStringLiteral("Close"));
    closeButton->setAutoRaise(true);

    m_output = new QPlainTextEdit(this);
    m_output->setReadOnly(true);
    m_output->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    QFont font(QStringLiteral("Ubuntu Mono"));
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(11);
    m_output->setFont(font);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(6, 4, 6, 4);
    header->setSpacing(8);
    header->addWidget(m_commands, 1);
    header->addWidget(m_status);
    header->addWidget(m_stop);
    header->addWidget(clearButton);
    header->addWidget(closeButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(header);
    layout->addWidget(m_output, 1);

    connect(m_commands, &QComboBox::currentIndexChanged, this, [this] { showSelected(); });
    connect(m_stop, &QPushButton::clicked, this, [this] { emit stopRequested(currentName()); });
    connect(clearButton, &QToolButton::clicked, this, [this] {
        const QString name = currentName();
        m_logs[name].text.clear();
        m_output->clear();
    });
    connect(closeButton, &QToolButton::clicked, this, [this] {
        m_closed = true;
        hide();
    });

    hide();
}

void OutputPanel::showCommand(const QString &name)
{
    ensureCommand(name);
    m_commands->setCurrentText(name);
    m_closed = false;
    show();
    showSelected();
}

void OutputPanel::appendOutput(const QString &name, const QString &text)
{
    ensureCommand(name);
    m_logs[name].text += text;
    if (currentName() == name) {
        m_output->moveCursor(QTextCursor::End);
        m_output->insertPlainText(text);
        m_output->moveCursor(QTextCursor::End);
    }
    if (!m_closed)
        show();
}

void OutputPanel::setState(const QString &name, bool running, int exitCode)
{
    ensureCommand(name);
    Log &log = m_logs[name];
    log.running = running;
    log.finished = !running;
    log.exitCode = exitCode;
    if (currentName() == name)
        showSelected();
}

void OutputPanel::ensureCommand(const QString &name)
{
    if (m_commands->findText(name) < 0)
        m_commands->addItem(name);
    if (!m_logs.contains(name))
        m_logs.insert(name, {});
}

QString OutputPanel::currentName() const
{
    return m_commands->currentText();
}

void OutputPanel::showSelected()
{
    if (!m_logs.contains(currentName()))
        return;
    const Log &log = m_logs[currentName()];
    m_output->setPlainText(log.text);
    m_output->moveCursor(QTextCursor::End);
    m_stop->setVisible(log.running);
    if (log.running)
        m_status->setText(QStringLiteral("Running"));
    else if (!log.finished)
        m_status->setText(QString());
    else if (log.exitCode < 0)
        m_status->setText(QStringLiteral("Stopped"));
    else
        m_status->setText(QStringLiteral("Exit code %1").arg(log.exitCode));
}
