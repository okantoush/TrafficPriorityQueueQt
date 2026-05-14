#include <QApplication>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>
#include "IntersectionWindow.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // ── Setup dialog: mode + grid size ───────────────────────────────
    QDialog dialog;
    dialog.setWindowTitle("Traffic Simulation");
    dialog.setFixedSize(380, 280);
    dialog.setStyleSheet("background-color: #1e1e2e;");

    QVBoxLayout* vlay = new QVBoxLayout(&dialog);
    vlay->setSpacing(14);
    vlay->setContentsMargins(24, 22, 24, 22);

    QLabel* title = new QLabel("Choose grid size and mode:");
    title->setStyleSheet("color: white; font-size: 15px; font-weight: bold;");
    title->setAlignment(Qt::AlignCenter);
    vlay->addWidget(title);

    // Grid-size row: two spinboxes for rows and cols.
    QHBoxLayout* gridLay = new QHBoxLayout;
    gridLay->setSpacing(10);

    auto makeLabel = [](const QString& txt) {
        QLabel* l = new QLabel(txt);
        l->setStyleSheet("color: #ddd; font-size: 13px;");
        return l;
    };

    QString spinStyle =
        "QSpinBox {"
        "  background:#2c2c3e; color:white; border:1px solid #555;"
        "  border-radius:4px; padding:4px; min-width:48px; font-size:14px;"
        "}";

    QSpinBox* rowsSpin = new QSpinBox;
    rowsSpin->setRange(1, 4);
    rowsSpin->setValue(2);
    rowsSpin->setStyleSheet(spinStyle);

    QSpinBox* colsSpin = new QSpinBox;
    colsSpin->setRange(1, 4);
    colsSpin->setValue(2);
    colsSpin->setStyleSheet(spinStyle);

    gridLay->addWidget(makeLabel("Rows:"));
    gridLay->addWidget(rowsSpin);
    gridLay->addSpacing(20);
    gridLay->addWidget(makeLabel("Cols:"));
    gridLay->addWidget(colsSpin);
    gridLay->addStretch();
    vlay->addLayout(gridLay);

    QLabel* hint = new QLabel(
        "<font color='#888' size='-1'>1×1 through 4×4. Cars and sprite "
        "sizes scale to fit the 620×620 viewport, so larger grids show "
        "smaller cars.</font>");
    hint->setTextFormat(Qt::RichText);
    hint->setWordWrap(true);
    vlay->addWidget(hint);

    QLabel* subtitle = new QLabel(
        "<font color='#888'>Simulation: scripted spawns use random graph routes<br>"
        "Manual: N / E / S / W or G — random perimeter entry → exit</font>");
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
    int  rows = 2, cols = 2;
    QObject::connect(simBtn, &QPushButton::clicked, [&]{
        manualMode = false;
        rows = rowsSpin->value();
        cols = colsSpin->value();
        dialog.accept();
    });
    QObject::connect(manBtn, &QPushButton::clicked, [&]{
        manualMode = true;
        rows = rowsSpin->value();
        cols = colsSpin->value();
        dialog.accept();
    });

    if (dialog.exec() != QDialog::Accepted)
        return 0;

    // ── Launch main window with the chosen grid size ─────────────────
    IntersectionWindow window(manualMode, rows, cols);
    QString modeStr = manualMode ? "Manual" : "Simulation";
    window.setWindowTitle(QString("Traffic Simulation — %1 (%2×%3)")
                          .arg(modeStr).arg(rows).arg(cols));
    window.show();

    return a.exec();
}
