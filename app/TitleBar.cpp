#include "app/TitleBar.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QToolButton>

namespace {

constexpr int kBarHeight = 38;
constexpr int kCaptionButtonWidth = 46;

// Segoe MDL2 Assets glyphs, the same ones native Windows caption bars use.
constexpr QChar kGlyphMinimize{0xE921};
constexpr QChar kGlyphMaximize{0xE922};
constexpr QChar kGlyphRestore{0xE923};
constexpr QChar kGlyphClose{0xE8BB};

} // namespace

TitleBar::TitleBar(QMenuBar *menuBar, QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("TitleBar"));
    // Plain QWidget subclasses ignore stylesheet backgrounds without this.
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(kBarHeight);

    auto *icon = new QLabel(this);
    icon->setPixmap(QApplication::windowIcon().pixmap(20, 20));

    menuBar->setParent(this);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setObjectName(QStringLiteral("titleLabel"));

    auto *minimizeButton = makeCaptionButton(kGlyphMinimize, QStringLiteral("minimizeButton"),
                                             QStringLiteral("Minimize"));
    m_maximizeButton = makeCaptionButton(kGlyphMaximize, QStringLiteral("maximizeButton"),
                                         QStringLiteral("Maximize"));
    auto *closeButton = makeCaptionButton(kGlyphClose, QStringLiteral("closeButton"),
                                          QStringLiteral("Close"));

    connect(minimizeButton, &QToolButton::clicked, this, [this] { window()->showMinimized(); });
    connect(m_maximizeButton, &QToolButton::clicked, this, [this] {
        if (window()->isMaximized())
            window()->showNormal();
        else
            window()->showMaximized();
    });
    connect(closeButton, &QToolButton::clicked, this, [this] { window()->close(); });

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(0);
    buttonRow->addWidget(minimizeButton);
    buttonRow->addWidget(m_maximizeButton);
    buttonRow->addWidget(closeButton);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 0, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(icon);
    layout->addWidget(menuBar, 0, Qt::AlignVCenter);
    layout->addStretch(1);
    layout->addWidget(m_titleLabel);
    layout->addStretch(1);
    layout->addLayout(buttonRow);
}

QToolButton *TitleBar::makeCaptionButton(QChar glyph, const QString &objectName,
                                         const QString &toolTip)
{
    auto *button = new QToolButton(this);
    button->setObjectName(objectName);
    button->setToolTip(toolTip);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedSize(kCaptionButtonWidth, kBarHeight);
    QFont glyphFont(QStringLiteral("Segoe MDL2 Assets"));
    glyphFont.setPixelSize(10);
    button->setFont(glyphFont);
    button->setText(glyph);
    return button;
}

bool TitleBar::isDraggableAt(const QPoint &pos) const
{
    QWidget *child = childAt(pos);
    return !child || qobject_cast<QLabel *>(child) != nullptr;
}

void TitleBar::setMaximized(bool maximized)
{
    m_maximizeButton->setText(maximized ? kGlyphRestore : kGlyphMaximize);
    m_maximizeButton->setToolTip(maximized ? QStringLiteral("Restore")
                                           : QStringLiteral("Maximize"));
}

void TitleBar::setTitle(const QString &title)
{
    m_titleLabel->setText(title);
}
