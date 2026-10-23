#include "ReactorWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QTimer>

#include <cmath>

ReactorWidget::ReactorWidget(QWidget *parent)
    : QWidget(parent), animationTimer_(new QTimer(this))
{
    setMinimumSize(280, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    animationTimer_->setInterval(40);
    connect(animationTimer_, &QTimer::timeout, this, [this] {
        phase_ += 0.035;
        if (phase_ > 6.283185307)
            phase_ = 0.0;
        update();
    });
    animationTimer_->start();
}

QSize ReactorWidget::sizeHint() const
{
    return QSize(390, 390);
}

void ReactorWidget::setState(const QString &state)
{
    if (state_ == state)
        return;
    state_ = state;
    update();
}

void ReactorWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QPointF center(width() / 2.0, height() / 2.0);
    const double radius = std::min(width(), height()) * 0.34;
    const double pulse = 0.5 + 0.5 * std::sin(phase_);
    const QColor cyan(89, 220, 255);
    const QColor ice(192, 246, 255);

    QRadialGradient glow(center, radius * 1.58);
    glow.setColorAt(0.0, QColor(14, 117, 159, 32));
    glow.setColorAt(0.55, QColor(8, 57, 83, 16));
    glow.setColorAt(1.0, QColor(4, 13, 24, 0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(glow);
    painter.drawEllipse(center, radius * 1.58, radius * 1.58);

    painter.setBrush(Qt::NoBrush);
    for (int i = 0; i < 4; ++i) {
        const double r = radius * (0.64 + 0.16 * i);
        QPen pen(QColor(62, 157, 190, 28 + i * 9), i == 0 ? 1.5 : 1.0);
        if (i == 3)
            pen.setStyle(Qt::DotLine);
        painter.setPen(pen);
        painter.drawEllipse(center, r, r);
    }

    QRectF outer(center.x() - radius, center.y() - radius, radius * 2.0, radius * 2.0);
    QPen arcPen(QColor(64, 205, 240, 110), 2.0, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(arcPen);
    painter.drawArc(outer.adjusted(-2, -2, 2, 2), static_cast<int>(phase_ * 90.0 * 16.0), 210 * 16);

    QPen brightPen(cyan, 3.0 + pulse * 0.7, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(brightPen);
    painter.drawArc(outer.adjusted(7, 7, -7, -7), 38 * 16, 74 * 16);
    painter.drawArc(outer.adjusted(7, 7, -7, -7), 218 * 16, 44 * 16);

    QRectF inner(center.x() - radius * 0.52, center.y() - radius * 0.52,
                 radius * 1.04, radius * 1.04);
    QRadialGradient core(center, radius * 0.64);
    core.setColorAt(0.0, QColor(24, 118, 156, 52 + static_cast<int>(pulse * 16)));
    core.setColorAt(0.72, QColor(7, 34, 52, 30));
    core.setColorAt(1.0, QColor(4, 14, 26, 0));
    painter.setPen(QPen(QColor(104, 224, 250, 135), 1.0));
    painter.setBrush(core);
    painter.drawEllipse(inner);

    const double spokeRadius = radius * 0.70;
    painter.setPen(QPen(QColor(107, 214, 241, 86), 1.0));
    for (int i = 0; i < 48; ++i) {
        const double angle = (6.283185307 * i) / 48.0;
        const double a = spokeRadius * (i % 4 == 0 ? 0.93 : 0.965);
        const QPointF p1(center.x() + std::cos(angle) * a, center.y() + std::sin(angle) * a);
        const QPointF p2(center.x() + std::cos(angle) * spokeRadius, center.y() + std::sin(angle) * spokeRadius);
        painter.drawLine(p1, p2);
    }

    painter.setPen(ice);
    QFont statusFont = painter.font();
    statusFont.setLetterSpacing(QFont::AbsoluteSpacing, 3.0);
    statusFont.setPointSize(19);
    statusFont.setWeight(QFont::DemiBold);
    painter.setFont(statusFont);
    painter.drawText(QRectF(center.x() - radius * 0.7, center.y() - 20, radius * 1.4, 32),
                     Qt::AlignCenter, state_);

    painter.setPen(QColor(133, 178, 194));
    QFont subFont = painter.font();
    subFont.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
    subFont.setPointSize(8);
    subFont.setWeight(QFont::Medium);
    painter.setFont(subFont);
    painter.drawText(QRectF(center.x() - radius * 0.8, center.y() + 17, radius * 1.6, 20),
                     Qt::AlignCenter, QStringLiteral("PERSONAL SYSTEM"));
}
