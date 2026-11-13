#include "app/TrustBar.h"

#include <QDialog>
#include <QObject>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QString barText(TrustDecision decision, const TrustDifference &difference)
{
    if (decision != TrustDecision::Mismatch)
        return QStringLiteral("This folder defines scripts that run on open.");
    QStringList parts = difference.commands + difference.paths;
    if (parts.isEmpty())
        return QStringLiteral("Scripts in this folder changed.");
    if (parts.size() > 4) {
        return QStringLiteral("Scripts in this folder changed: %1, and %2 more.")
            .arg(parts.mid(0, 3).join(QStringLiteral(", ")))
            .arg(parts.size() - 3);
    }
    return QStringLiteral("Scripts in this folder changed: %1.").arg(parts.join(QStringLiteral(", ")));
}

} // namespace

TrustBar::TrustBar(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    m_label = new QLabel(this);
    m_label->setWordWrap(true);
    auto *review = new QPushButton(QStringLiteral("Review"), this);
    auto *trust = new QPushButton(QStringLiteral("Trust"), this);
    auto *stay = new QPushButton(QStringLiteral("Stay restricted"), this);
    layout->addWidget(m_label, 1);
    layout->addWidget(review);
    layout->addWidget(trust);
    layout->addWidget(stay);
    connect(review, &QPushButton::clicked, this, &TrustBar::reviewRequested);
    connect(trust, &QPushButton::clicked, this, &TrustBar::trustRequested);
    connect(stay, &QPushButton::clicked, this, &TrustBar::restrictRequested);
    hide();
}

void TrustBar::showDecision(TrustDecision decision, const TrustDifference &difference)
{
    m_label->setText(barText(decision, difference));
    show();
}

TrustChoice execTrustReview(QWidget *parent, const QString &text)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Workspace scripts"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *view = new QPlainTextEdit(text, &dialog);
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(view);
    auto *buttons = new QDialogButtonBox(&dialog);
    auto *trust = buttons->addButton(QStringLiteral("Trust"), QDialogButtonBox::AcceptRole);
    auto *stay = buttons->addButton(QStringLiteral("Stay restricted"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    TrustChoice choice = TrustChoice::Close;
    QObject::connect(trust, &QPushButton::clicked, &dialog, [&] {
        choice = TrustChoice::Trust;
        dialog.accept();
    });
    QObject::connect(stay, &QPushButton::clicked, &dialog, [&] {
        choice = TrustChoice::Restrict;
        dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(640, 420);
    dialog.exec();
    return choice;
}
