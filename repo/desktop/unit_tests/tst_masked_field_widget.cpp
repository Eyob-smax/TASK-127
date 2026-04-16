// tst_masked_field_widget.cpp — ProctorOps
// Unit tests for MaskedFieldWidget behavior (mask/reveal/remask/copy).

#include <QtTest/QtTest>

#include <QApplication>
#include <QClipboard>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>

#include "widgets/MaskedFieldWidget.h"

class TstMaskedFieldWidget : public QObject {
    Q_OBJECT

private slots:
    void test_setValue_showsMaskedText();
    void test_revealButton_emitsRequest();
    void test_revealAndRemask_toggleDisplay();
    void test_copyButton_copiesMaskedValue();

private:
    static QPushButton* findButtonByText(const MaskedFieldWidget& widget,
                                         const QString& text);
};

QPushButton* TstMaskedFieldWidget::findButtonByText(const MaskedFieldWidget& widget,
                                                    const QString& text)
{
    const auto buttons = widget.findChildren<QPushButton*>();
    for (QPushButton* button : buttons) {
        if (button->text() == text)
            return button;
    }
    return nullptr;
}

void TstMaskedFieldWidget::test_setValue_showsMaskedText()
{
    MaskedFieldWidget widget;
    widget.setValue(QStringLiteral("1234567890"));

    const QLabel* label = widget.findChild<QLabel*>();
    QVERIFY(label != nullptr);
    QVERIFY(label->text().endsWith(QStringLiteral("7890")));
    QVERIFY(label->text().contains(QChar('*')));
    QVERIFY(!widget.isRevealed());
}

void TstMaskedFieldWidget::test_revealButton_emitsRequest()
{
    MaskedFieldWidget widget;
    widget.setValue(QStringLiteral("1234567890"));

    QSignalSpy spy(&widget, &MaskedFieldWidget::revealRequested);
    QPushButton* revealButton = findButtonByText(widget, QStringLiteral("Reveal"));
    QVERIFY(revealButton != nullptr);

    QTest::mouseClick(revealButton, Qt::LeftButton);
    QCOMPARE(spy.count(), 1);
}

void TstMaskedFieldWidget::test_revealAndRemask_toggleDisplay()
{
    MaskedFieldWidget widget;
    widget.setValue(QStringLiteral("1234567890"));

    QLabel* label = widget.findChild<QLabel*>();
    QVERIFY(label != nullptr);

    widget.reveal();
    QVERIFY(widget.isRevealed());
    QCOMPARE(label->text(), QStringLiteral("1234567890"));

    widget.remask();
    QVERIFY(!widget.isRevealed());
    QVERIFY(label->text().endsWith(QStringLiteral("7890")));
    QVERIFY(label->text().contains(QChar('*')));
}

void TstMaskedFieldWidget::test_copyButton_copiesMaskedValue()
{
    MaskedFieldWidget widget;
    widget.setValue(QStringLiteral("1234567890"));

    QPushButton* copyButton = findButtonByText(widget, QStringLiteral("Copy"));
    QVERIFY(copyButton != nullptr);

    QTest::mouseClick(copyButton, Qt::LeftButton);
    const QString clipboardText = QApplication::clipboard()->text();

    QVERIFY(clipboardText.endsWith(QStringLiteral("7890")));
    QVERIFY(clipboardText.contains(QChar('*')));
    QVERIFY(clipboardText != QStringLiteral("1234567890"));
}

QTEST_MAIN(TstMaskedFieldWidget)
#include "tst_masked_field_widget.moc"
