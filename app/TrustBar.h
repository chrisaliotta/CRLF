#pragma once

#include "io/ProjectTrust.h"

#include <QWidget>

class QLabel;

// The strip under the find bar. It stays up until the folder is trusted or
// the user chooses to keep it restricted.
class TrustBar : public QWidget
{
    Q_OBJECT

public:
    explicit TrustBar(QWidget *parent = nullptr);

    void showDecision(TrustDecision decision, const TrustDifference &difference);

signals:
    void reviewRequested();
    void trustRequested();
    void restrictRequested();

private:
    QLabel *m_label = nullptr;
};

enum class TrustChoice { Close, Trust, Restrict };

// Review lists the gated commands and the scripts they pin.
TrustChoice execTrustReview(QWidget *parent, const QString &text);
