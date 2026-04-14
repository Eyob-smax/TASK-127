// tst_action_routing.cpp — ProctorOps
// Unit tests for ActionRouter: registration, dispatch, filter, shortcut lookup.

#include <QtTest>
#include "app/ActionRouter.h"

class TstActionRouting : public QObject {
    Q_OBJECT

private slots:
    void registerAndDispatch();
    void duplicateRegistrationReplaces();
    void dispatchUnknownReturnsFalse();
    void dispatchCallsHandlerMultipleTimes();
    void filterByDisplayName();
    void filterByCategory();
    void filterByActionId();
    void filterEmptyReturnsAll();
    void filterIsCaseInsensitive();
    void shortcutLookupFindsAction();
    void shortcutNotRegisteredReturnsNullptr();
    void emptyShortcutNotMatched();
    void actionsChangedOnRegister();
    void actionsChangedOnReplace();
    void allActionsReturnsAll();
    void advancedFilterDispatchNoRecursion();
};

void TstActionRouting::registerAndDispatch()
{
    ActionRouter router;
    bool called = false;
    router.registerAction({
        QStringLiteral("test.action"),
        QStringLiteral("Test Action"),
        QStringLiteral("Test"),
        {},
        false,
        [&called]{ called = true; }
    });
    const bool found = router.dispatch(QStringLiteral("test.action"));
    QVERIFY(found);
    QVERIFY(called);
}

void TstActionRouting::duplicateRegistrationReplaces()
{
    ActionRouter router;
    int callResult = 0;
    router.registerAction({QStringLiteral("a"), QStringLiteral("Old"), QStringLiteral("X"),
                           {}, false, [&callResult]{ callResult = 1; }});
    router.registerAction({QStringLiteral("a"), QStringLiteral("New"), QStringLiteral("X"),
                           {}, false, [&callResult]{ callResult = 2; }});

    router.dispatch(QStringLiteral("a"));
    QCOMPARE(callResult, 2);
    QCOMPARE(router.allActions().size(), 1);
    QCOMPARE(router.allActions().first().displayName, QStringLiteral("New"));
}

void TstActionRouting::dispatchUnknownReturnsFalse()
{
    ActionRouter router;
    QVERIFY(!router.dispatch(QStringLiteral("nonexistent.action")));
}

void TstActionRouting::dispatchCallsHandlerMultipleTimes()
{
    ActionRouter router;
    int n = 0;
    router.registerAction({QStringLiteral("x"), QStringLiteral("X"), QStringLiteral("X"),
                           {}, false, [&n]{ ++n; }});
    router.dispatch(QStringLiteral("x"));
    router.dispatch(QStringLiteral("x"));
    router.dispatch(QStringLiteral("x"));
    QCOMPARE(n, 3);
}

void TstActionRouting::filterByDisplayName()
{
    ActionRouter router;
    router.registerAction({QStringLiteral("a"), QStringLiteral("Open Check-In Desk"),
                           QStringLiteral("Windows"), {}, false, {}});
    router.registerAction({QStringLiteral("b"), QStringLiteral("Open Question Bank"),
                           QStringLiteral("Windows"), {}, false, {}});
    router.registerAction({QStringLiteral("c"), QStringLiteral("Lock Console"),
                           QStringLiteral("Shell"), {}, false, {}});

    const auto results = router.filter(QStringLiteral("check"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().id, QStringLiteral("a"));
}

void TstActionRouting::filterByCategory()
{
    ActionRouter router;
    router.registerAction({QStringLiteral("a"), QStringLiteral("Alpha"),
                           QStringLiteral("Shell"), {}, false, {}});
    router.registerAction({QStringLiteral("b"), QStringLiteral("Beta"),
                           QStringLiteral("Content"), {}, false, {}});
    router.registerAction({QStringLiteral("c"), QStringLiteral("Gamma"),
                           QStringLiteral("Shell"), {}, false, {}});

    const auto results = router.filter(QStringLiteral("Shell"));
    QCOMPARE(results.size(), 2);
}

void TstActionRouting::filterByActionId()
{
    ActionRouter router;
    router.registerAction({QStringLiteral("audit.view"),  QStringLiteral("View Audit"),
                           QStringLiteral("Admin"), {}, false, {}});
    router.registerAction({QStringLiteral("member.mask"), QStringLiteral("Mask PII"),
                           QStringLiteral("Members"), {}, false, {}});

    const auto results = router.filter(QStringLiteral("audit"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().id, QStringLiteral("audit.view"));
}

void TstActionRouting::filterEmptyReturnsAll()
{
    ActionRouter router;
    router.registerAction({QStringLiteral("a"), QStringLiteral("A"), QStringLiteral("X"),
                           {}, false, {}});
    router.registerAction({QStringLiteral("b"), QStringLiteral("B"), QStringLiteral("Y"),
                           {}, false, {}});
    router.registerAction({QStringLiteral("c"), QStringLiteral("C"), QStringLiteral("Z"),
                           {}, false, {}});
    QCOMPARE(router.filter(QString()).size(), 3);
}

void TstActionRouting::filterIsCaseInsensitive()
{
    ActionRouter router;
    router.registerAction({QStringLiteral("lock"), QStringLiteral("Lock Console"),
                           QStringLiteral("Shell"), {}, false, {}});
    QCOMPARE(router.filter(QStringLiteral("LOCK")).size(), 1);
    QCOMPARE(router.filter(QStringLiteral("lock")).size(), 1);
    QCOMPARE(router.filter(QStringLiteral("Lock")).size(), 1);
}

void TstActionRouting::shortcutLookupFindsAction()
{
    ActionRouter router;
    const QKeySequence ks(Qt::CTRL | Qt::Key_K);
    router.registerAction({
        QStringLiteral("palette"),
        QStringLiteral("Command Palette"),
        QStringLiteral("Shell"),
        ks,
        false,
        {}
    });
    const auto* found = router.findByShortcut(ks);
    QVERIFY(found != nullptr);
    QCOMPARE(found->id, QStringLiteral("palette"));
}

void TstActionRouting::shortcutNotRegisteredReturnsNullptr()
{
    ActionRouter router;
    QVERIFY(router.findByShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z)) == nullptr);
}

void TstActionRouting::emptyShortcutNotMatched()
{
    ActionRouter router;
    router.registerAction({QStringLiteral("no_shortcut"), QStringLiteral("No Shortcut"),
                           QStringLiteral("X"), {}, false, {}});
    // Empty key sequence should never match
    QVERIFY(router.findByShortcut(QKeySequence()) == nullptr);
}

void TstActionRouting::actionsChangedOnRegister()
{
    ActionRouter router;
    int count = 0;
    connect(&router, &ActionRouter::actionsChanged, [&count]{ ++count; });
    router.registerAction({QStringLiteral("a"), QStringLiteral("A"), QStringLiteral("X"),
                           {}, false, {}});
    router.registerAction({QStringLiteral("b"), QStringLiteral("B"), QStringLiteral("Y"),
                           {}, false, {}});
    QCOMPARE(count, 2);
}

void TstActionRouting::actionsChangedOnReplace()
{
    ActionRouter router;
    int count = 0;
    connect(&router, &ActionRouter::actionsChanged, [&count]{ ++count; });
    router.registerAction({QStringLiteral("a"), QStringLiteral("A"), QStringLiteral("X"),
                           {}, false, {}});
    router.registerAction({QStringLiteral("a"), QStringLiteral("A-new"), QStringLiteral("X"),
                           {}, false, {}});
    QCOMPARE(count, 2); // one for initial, one for replace
}

void TstActionRouting::allActionsReturnsAll()
{
    ActionRouter router;
    for (int i = 0; i < 5; ++i) {
        router.registerAction({
            QString::number(i), QString::number(i), QStringLiteral("X"), {}, false, {}
        });
    }
    QCOMPARE(router.allActions().size(), 5);
}

void TstActionRouting::advancedFilterDispatchNoRecursion()
{
    // Regression test modelling the actual MainShell Ctrl+F dispatch pattern.
    //
    // Before the fix, MainShell had:
    //   onAdvancedFilter()  →  router.dispatch("shell.advanced_filter")
    //   registered handler  →  onAdvancedFilter()               ← infinite loop
    //
    // After the fix:
    //   onAdvancedFilter()  →  direct QMetaObject::invokeMethod (no router)
    //   registered handler  →  onAdvancedFilter()               ← single call, no loop
    //
    // This test mirrors the fixed pattern: the registered action handler calls
    // a function that does real work (increments counter) without re-dispatching.
    // We also verify the shortcut lookup works (Ctrl+F).

    ActionRouter router;
    int directCallCount = 0;
    int dispatchCount    = 0;

    // Simulate onAdvancedFilter — does direct work, never re-enters the router.
    auto simulatedOnAdvancedFilter = [&directCallCount]{ ++directCallCount; };

    router.registerAction({
        QStringLiteral("shell.advanced_filter"),
        QStringLiteral("Advanced Filter"),
        QStringLiteral("Shell"),
        QKeySequence(Qt::CTRL | Qt::Key_F),
        true,
        [&simulatedOnAdvancedFilter, &dispatchCount]{
            ++dispatchCount;
            simulatedOnAdvancedFilter();
        }
    });

    // 1. Dispatch via action ID (simulates router action invocation).
    const bool found = router.dispatch(QStringLiteral("shell.advanced_filter"));
    QVERIFY(found);
    QCOMPARE(dispatchCount, 1);
    QCOMPARE(directCallCount, 1);

    // 2. Shortcut lookup must resolve to the same action.
    const auto* action = router.findByShortcut(QKeySequence(Qt::CTRL | Qt::Key_F));
    QVERIFY(action != nullptr);
    QCOMPARE(action->id, QStringLiteral("shell.advanced_filter"));

    // 3. Invoking the handler via shortcut lookup triggers exactly one more call.
    if (action && action->handler) action->handler();
    QCOMPARE(dispatchCount, 2);
    QCOMPARE(directCallCount, 2);
}

QTEST_MAIN(TstActionRouting)
#include "tst_action_routing.moc"
