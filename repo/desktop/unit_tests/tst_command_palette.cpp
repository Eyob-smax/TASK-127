// tst_command_palette.cpp — ProctorOps
// Unit tests for CommandPaletteDialog action filtering and dispatch behavior.
// Tests the ActionRouter filter logic that backs the command palette list.
// Dialog keyboard interaction is validated through ActionRouter directly.

#include <QtTest>
#include "app/ActionRouter.h"
#include "dialogs/CommandPaletteDialog.h"

class TstCommandPalette : public QObject {
    Q_OBJECT

private slots:
    void emptyFilterReturnsAllActions();
    void filterNarrowsActionList();
    void filterMatchesCategory();
    void filterMatchesId();
    void filteredListContainsExpectedIds();
    void noMatchingFilterReturnsEmpty();
    void dispatchByIdExecutesHandler();
    void paletteConstructsWithoutCrash();
};

void TstCommandPalette::emptyFilterReturnsAllActions()
{
    ActionRouter router;
    router.registerAction({QStringLiteral("a"), QStringLiteral("Alpha"),   QStringLiteral("X"), {}, false, {}});
    router.registerAction({QStringLiteral("b"), QStringLiteral("Beta"),    QStringLiteral("Y"), {}, false, {}});
    router.registerAction({QStringLiteral("c"), QStringLiteral("Charlie"), QStringLiteral("Z"), {}, false, {}});

    QCOMPARE(router.filter(QString()).size(), 3);
}

void TstCommandPalette::filterNarrowsActionList()
{
    ActionRouter router;
    router.registerAction({QStringLiteral("check.open"),    QStringLiteral("Open Check-In Desk"),
                           QStringLiteral("Windows"), {}, false, {}});
    router.registerAction({QStringLiteral("question.open"), QStringLiteral("Open Question Bank"),
                           QStringLiteral("Windows"), {}, false, {}});
    router.registerAction({QStringLiteral("shell.lock"),    QStringLiteral("Lock Console"),
                           QStringLiteral("Shell"),   {}, false, {}});

    const auto results = router.filter(QStringLiteral("lock"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().id, QStringLiteral("shell.lock"));
}

void TstCommandPalette::filterMatchesCategory()
{
    ActionRouter router;
    router.registerAction({QStringLiteral("a"), QStringLiteral("Action A"),
                           QStringLiteral("Admin"), {}, false, {}});
    router.registerAction({QStringLiteral("b"), QStringLiteral("Action B"),
                           QStringLiteral("Admin"), {}, false, {}});
    router.registerAction({QStringLiteral("c"), QStringLiteral("Action C"),
                           QStringLiteral("Members"), {}, false, {}});

    const auto results = router.filter(QStringLiteral("Admin"));
    QCOMPARE(results.size(), 2);
}

void TstCommandPalette::filterMatchesId()
{
    ActionRouter router;
    router.registerAction({QStringLiteral("audit.view"),  QStringLiteral("View Audit Log"),
                           QStringLiteral("Admin"), {}, false, {}});
    router.registerAction({QStringLiteral("audit.chain"), QStringLiteral("Verify Chain"),
                           QStringLiteral("Admin"), {}, false, {}});
    router.registerAction({QStringLiteral("member.mask"), QStringLiteral("Mask PII"),
                           QStringLiteral("Members"), {}, false, {}});

    const auto results = router.filter(QStringLiteral("audit"));
    QCOMPARE(results.size(), 2);
}

void TstCommandPalette::filteredListContainsExpectedIds()
{
    ActionRouter router;
    router.registerAction({QStringLiteral("window.checkin"),     QStringLiteral("Open Check-In Desk"),
                           QStringLiteral("Windows"), {}, false, {}});
    router.registerAction({QStringLiteral("window.questionbank"),QStringLiteral("Open Question Bank"),
                           QStringLiteral("Windows"), {}, false, {}});
    router.registerAction({QStringLiteral("window.auditviewer"), QStringLiteral("Open Audit Viewer"),
                           QStringLiteral("Windows"), {}, false, {}});
    router.registerAction({QStringLiteral("shell.lock"),         QStringLiteral("Lock Console"),
                           QStringLiteral("Shell"), {}, false, {}});

    const auto results = router.filter(QStringLiteral("window"));
    QCOMPARE(results.size(), 3);
    QStringList ids;
    for (const auto& a : results) ids << a.id;
    QVERIFY(ids.contains(QStringLiteral("window.checkin")));
    QVERIFY(ids.contains(QStringLiteral("window.questionbank")));
    QVERIFY(ids.contains(QStringLiteral("window.auditviewer")));
}

void TstCommandPalette::noMatchingFilterReturnsEmpty()
{
    ActionRouter router;
    router.registerAction({QStringLiteral("a"), QStringLiteral("Alpha"),
                           QStringLiteral("X"), {}, false, {}});
    const auto results = router.filter(QStringLiteral("zzz_no_match"));
    QCOMPARE(results.size(), 0);
}

void TstCommandPalette::dispatchByIdExecutesHandler()
{
    ActionRouter router;
    bool executed = false;
    router.registerAction({
        QStringLiteral("test.exec"),
        QStringLiteral("Execute Test"),
        QStringLiteral("Test"),
        {},
        false,
        [&executed]{ executed = true; }
    });
    QVERIFY(router.dispatch(QStringLiteral("test.exec")));
    QVERIFY(executed);
}

void TstCommandPalette::paletteConstructsWithoutCrash()
{
    // Verify the dialog can be constructed with an empty router (offscreen platform)
    ActionRouter router;
    router.registerAction({QStringLiteral("a"), QStringLiteral("A"), QStringLiteral("X"),
                           {}, false, {}});
    CommandPaletteDialog palette(router);
    // Construction must not crash in headless mode
    QVERIFY(true);
}

QTEST_MAIN(TstCommandPalette)
#include "tst_command_palette.moc"
