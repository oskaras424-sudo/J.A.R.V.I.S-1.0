#pragma once

#include <QWidget>

class QTimer;

class ReactorWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit ReactorWidget(QWidget *parent = nullptr);
    QSize sizeHint() const override;

public slots:
    void setState(const QString &state);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString state_ = QStringLiteral("IDLE");
    QTimer *animationTimer_ = nullptr;
    double phase_ = 0.0;
};
