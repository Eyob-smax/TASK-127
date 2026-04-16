// tst_app_context.cpp — ProctorOps
// Unit tests for AppContext aggregate ownership and default session state.

#include <QtTest/QtTest>

#include "AppContextTestTypes.h"

class TstAppContext : public QObject {
    Q_OBJECT

private slots:
    void test_defaultAuthenticationState();
    void test_acceptsOwnedComponents();
};

void TstAppContext::test_defaultAuthenticationState()
{
    AppContext ctx;
    QVERIFY(!ctx.authenticated);
    QVERIFY(ctx.session.userId.isEmpty());
}

void TstAppContext::test_acceptsOwnedComponents()
{
    AppContext ctx;

    // Build minimal owning pointers and verify they are retained.
    ctx.keyStore = std::make_unique<KeyStore>(QStringLiteral("."));
    ctx.cipher = std::make_unique<AesGcmCipher>(QByteArray(32, '\x11'));

    QVERIFY(ctx.keyStore != nullptr);
    QVERIFY(ctx.cipher != nullptr);
}

QTEST_GUILESS_MAIN(TstAppContext)
#include "tst_app_context.moc"
