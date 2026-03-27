#include <QApplication>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include "IntersectionWindow.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // ── Mode selection dialog ─────────────────────────────────────────
    QDialog dialog;
    dialog.setWindowTitle("Traffic Simulation");
    dialog.setFixedSize(340, 160);
    dialog.setStyleSheet("background-color: #1e1e2e;");

    QVBoxLayout* vlay = new QVBoxLayout(&dialog);
    vlay->setSpacing(16);
    vlay->setContentsMargins(24, 24, 24, 24);

    QLabel* title = new QLabel("Choose a mode to start:");
    title->setStyleSheet("color: white; font-size: 15px; font-weight: bold;");
    title->setAlignment(Qt::AlignCenter);
    vlay->addWidget(title);

    QLabel* subtitle = new QLabel(
        "<font color='#888'>Simulation: 8 cars auto-spawn<br>"
        "Manual: spawn cars with N / E / S / W keys</font>");
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setTextFormat(Qt::RichText);
    vlay->addWidget(subtitle);

    QHBoxLayout* hlay = new QHBoxLayout;
    hlay->setSpacing(12);

    QString btnStyle =
        "QPushButton {"
        "  color: white; border-radius: 8px;"
        "  font-size: 13px; font-weight: bold; padding: 8px 0;"
        "}"
        "QPushButton:hover  { opacity: 0.85; }"
        "QPushButton:pressed{ opacity: 0.70; }";

    QPushButton* simBtn = new QPushButton("▶  Simulation");
    simBtn->setStyleSheet(btnStyle + "QPushButton { background:#3a7bd5; }");
    simBtn->setFixedHeight(38);

    QPushButton* manBtn = new QPushButton("⌨  Manual");
    manBtn->setStyleSheet(btnStyle + "QPushButton { background:#2ecc71; }");
    manBtn->setFixedHeight(38);

    hlay->addWidget(simBtn);
    hlay->addWidget(manBtn);
    vlay->addLayout(hlay);

    bool manualMode = false;
    QObject::connect(simBtn, &QPushButton::clicked, [&]{ manualMode = false; dialog.accept(); });
    QObject::connect(manBtn, &QPushButton::clicked, [&]{ manualMode = true;  dialog.accept(); });

    if (dialog.exec() != QDialog::Accepted)
        return 0;

    // ── Launch main window ────────────────────────────────────────────
    IntersectionWindow window(manualMode);
    window.setWindowTitle(manualMode ? "Traffic Simulation — Manual Mode"
                                     : "Traffic Simulation — Simulation Mode");
    window.show();

    return a.exec();
}
