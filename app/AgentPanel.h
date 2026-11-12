#pragma once

#include "agent/AcpTypes.h"

#include <QList>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTextBrowser;
class QTimer;
class QToolButton;
class QVBoxLayout;

// One button on a card. `closes` false leaves the card up, as Open Diff does.
struct AgentChoice
{
    QString label;
    std::function<void()> chosen;
    bool closes = true;
};

// The conversation with the agent: a transcript, the questions waiting for
// an answer, and the prompt box. It shows what it is told and reports what
// the user does; AgentController and AgentHost decide. See docs/agent.md.
class AgentPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AgentPanel(QWidget *parent = nullptr);

    void setAgents(const QStringList &names, const QString &current);
    QString currentAgent() const;
    // One control per setting the agent offers, in its order: a list for a
    // select, a check box for a boolean. An empty list hides the row.
    void setConfigOptions(const QList<AgentConfigOption> &options);
    // On: the agent's thinking is drawn in full as it streams. Off: one
    // "Thinking" line while it streams, and nothing afterwards.
    void setShowThinking(bool show);
    void setStatus(const QString &text);
    // While busy, Send is Stop.
    void setBusy(bool busy);
    void clearTranscript();
    void addUserMessage(const QString &text);
    void addNote(const QString &text);
    void applyUpdate(const AgentUpdate &update);
    // A card that stays until one of its choices closes it. `dismissed` runs
    // instead when dismissAll() clears it first.
    void ask(const QString &title, const QString &detail, const QList<AgentChoice> &choices,
             std::function<void()> dismissed);
    void dismissAll();
    void focusInput();
    QSize sizeHint() const override { return {380, 600}; }

signals:
    void agentChosen(const QString &name);
    void restartRequested();
    void configOptionChosen(const QString &id, const QVariant &value);
    void showThinkingToggled(bool show);
    void promptSubmitted(const QString &text, bool attachCurrentFile);
    void cancelRequested();
    void closeRequested();
    void locationActivated(const QString &path, int line);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Entry
    {
        enum class Kind { User, Agent, Thought, Tool, Plan, Note };
        Kind kind = Kind::Note;
        QString text;
        QString toolCallId;
        QString title;
        QString status;
        QList<AgentToolLocation> locations;
        QList<AgentPlanEntry> plan;
    };
    struct Card
    {
        QWidget *widget = nullptr;
        std::function<void()> dismissed;
    };

    void submit();
    // Appends a chunk to the last entry when it is the same kind of text.
    void appendText(Entry::Kind kind, const QString &text);
    void applyTool(const AgentUpdate &update);
    void scheduleRender();
    void render();
    QString markdown() const;
    void closeCard(QWidget *widget);

    QComboBox *m_agents = nullptr;
    QToolButton *m_thinking = nullptr;
    QWidget *m_options = nullptr;
    QGridLayout *m_optionLayout = nullptr;
    QLabel *m_status = nullptr;
    QTextBrowser *m_transcript = nullptr;
    QWidget *m_cards = nullptr;
    QVBoxLayout *m_cardLayout = nullptr;
    QPlainTextEdit *m_input = nullptr;
    QCheckBox *m_attach = nullptr;
    QPushButton *m_send = nullptr;
    QTimer *m_render = nullptr;
    QList<Entry> m_entries;
    QList<Card> m_open;
    bool m_busy = false;
};
