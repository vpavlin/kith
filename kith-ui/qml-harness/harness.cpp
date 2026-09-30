// Offscreen render harness for Main.qml (kith_ui). Injects a mock `logos` context object
// (callModule returns canned JSON for "kith" and "loam_core"; with KITH_MOCK_ASYNC=1 the
// mock ALSO has callModuleAsync - the newer Basecamp bridge - and any SYNC callModule the
// view makes is flagged [SYNC-CALL!]) so the view renders without
// the Basecamp host or the real kith core. Prints every QML runtime message to stderr, then
// screenshots several surfaces so we can SEE what the user sees.
//   usage: harness <Main.qml> <outDir>
#include <QGuiApplication>
#include <QQuickView>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickItem>
#include <QObject>
#include <QVariant>
#include <QTimer>
#include <QImage>
#include <QDebug>
#include <QJSValue>
#include <QJSEngine>
#include <cstdlib>

class MockLogos : public QObject {
    Q_OBJECT
public:
    explicit MockLogos(QObject *p = nullptr) : QObject(p) {}
    bool bookDeleted = false;
    // NOTE: defined OUT OF LINE below — moc's parser chokes on raw string literals inside
    // an inline class body, so keep the class declaration clean.
    Q_INVOKABLE QString callModule(const QString &mod, const QString &method, const QVariant &args);
    QString reply(const QString &mod, const QString &method, const QVariant &args);
    bool asyncMode = false;
    int syncCalls = 0;
};

// Newer-bridge mock: callModuleAsync(module, method, args, cb, timeoutMs) answering on a
// later tick (like the real LogosQmlBridge), with a small latency.
class MockLogosAsync : public MockLogos {
    Q_OBJECT
public:
    explicit MockLogosAsync(QObject *p = nullptr) : MockLogos(p) { asyncMode = true; }
    int inFlight = 0, maxInFlight = 0, asyncCalls = 0;
    Q_INVOKABLE void callModuleAsync(const QString &mod, const QString &method, const QVariantList &args,
                                     QJSValue cb, int timeoutMs = 30000) {
        Q_UNUSED(timeoutMs);
        ++asyncCalls; ++inFlight; if (inFlight > maxInFlight) maxInFlight = inFlight;
        const QString r = reply(mod, method, QVariant(args));
        QTimer::singleShot(40, this, [this, cb, r, method]() mutable {
            --inFlight;
            if (!cb.isCallable()) return;
            QJSValue ret = cb.call({ QJSValue(r) });
            if (ret.isError()) fprintf(stderr, "[cb-err] %s: %s\n", qPrintable(method), qPrintable(ret.toString()));
            if (getenv("KITH_MOCK_TRACE")) fprintf(stderr, "[reply] %s\n", qPrintable(method));
        });
    }
};

QString MockLogos::callModule(const QString &mod, const QString &method, const QVariant &args) {
    ++syncCalls;
    if (asyncMode) fprintf(stderr, "[SYNC-CALL!] %s.%s - view used blocking callModule on an async bridge\n", qPrintable(mod), qPrintable(method));
    return reply(mod, method, args);
}

QString MockLogos::reply(const QString &mod, const QString &method, const QVariant &args) {
    const QVariantList a = args.toList();
    if (method != "listBooks" && method != "listContacts" && method != "listIdentities" && method != "getDefaultIdentityId") {
        QStringList parts;
        for (const auto &v : a) parts << v.toString();
        fprintf(stderr, "[CALL] %s.%s(%s)\n", qPrintable(mod), qPrintable(method), qPrintable(parts.join(" | ")));
    }
    if (mod == "loam_core") {
        if (method == "listIdentities")
            return QString(R"([{"id":"device","kind":"device","label":"This device","address":"0xme00000000000000000000000000000000000000","pubHex":"02aa"},{"id":"soft-1","kind":"soft","label":"Work","address":"0xwork1111111111111111111111111111111111","pubHex":"03bb"}])");
        if (method == "getDefaultIdentityId") return QString(R"("device")");
        if (method == "identityForContainer") return QString(R"({"id":"device","kind":"device","label":"This device","address":"0xme00000000000000000000000000000000000000","pubHex":"02aa"})");
        return "";
    }
    // mod == "kith"
    if (method == "coreVersion") return "\"0.2.3\"";
    if (method == "getIdentity") return "\"0xme00000000000000000000000000000000000000\"";
    if (method == "listBooks") {
        if (bookDeleted) return "[]";
        return QString(R"([{"id":"b1","name":"Personal","authorAddr":"0xme00000000000000000000000000000000000000","contactCount":2}])");
    }
    if (method == "listContacts") {
        return QString(R"([
          {"id":"c1","name":{"display":"Ada Lovelace","given":"Ada","family":"Lovelace","org":""},
           "phones":[{"label":"mobile","value":"+1 555 0100"}],
           "emails":[{"label":"home","value":"ada@example.com"}],
           "handles":[{"kind":"telegram","value":"@ada"}],
           "addresses":[],"notes":"Met at a conference.",
           "loamIdentity":{"address":"0xcard2222222222222222222222222222222222","pubHex":"02cc","verified":true,"addedVia":"manual"},
           "authorAddr":"0xme00000000000000000000000000000000000000","createdAt":1000,"updatedAt":1000},
          {"id":"c2","name":{"display":"Bob Example","given":"Bob","family":"Example","org":"Acme"},
           "phones":[],"emails":[{"label":"work","value":"bob@acme.test"}],
           "handles":[],"addresses":[{"label":"home","street":"1 Main St","city":"Springfield","region":"","postcode":"","country":"US"}],
           "notes":"","authorAddr":"0xme00000000000000000000000000000000000000","createdAt":900,"updatedAt":900}
        ])");
    }
    if (method == "createBook") return "\"bNEW\"";
    if (method == "deleteBook") { bookDeleted = true; return "true"; }
    if (method == "addContact") return "\"cNEW\"";
    if (method == "editContact") return "\"c1\"";
    if (method == "deleteContact") return "true";
    if (method == "importVcard") return QString(R"({"imported":1,"ids":["cX"]})");
    if (method == "exportVcard") return QString("\"BEGIN:VCARD\\nVERSION:4.0\\nFN:Ada Lovelace\\nEND:VCARD\\n\"");
    if (method == "shareLink") return QString("\"kith://join?id=b1&key=bW9ja2tleQ&name=Personal\"");
    if (method == "qrMatrix") {
        // Mock matrix: a deterministic checkerboard-ish pattern (NOT a real scannable
        // QR — the harness only proves the core->view->Canvas draw pipeline works).
        const int n = 21;
        QString cells;
        for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) {
            bool border = (x < 4 && y < 4) || (x >= n - 4 && y < 4) || (x < 4 && y >= n - 4); // finder-like corners
            bool on = border || ((x + y) % 3 == 0);
            cells += (on ? "1" : "0"); if (!(y == n - 1 && x == n - 1)) cells += ",";
        }
        return QString(R"({"ok":true,"n":%1,"cells":[%2]})").arg(n).arg(cells);
    }
    return "";
}

static void grab(QQuickView *v, const QString &path) {
    QImage img = v->grabWindow();
    if (!img.isNull()) { img.save(path); fprintf(stderr, "[shot] %s (%dx%d)\n", qPrintable(path), img.width(), img.height()); }
    else fprintf(stderr, "[shot] NULL image for %s\n", qPrintable(path));
}
static void runJs(QQuickView *v, const QString &js) {
    QQmlContext *ctx = QQmlEngine::contextForObject(v->rootObject());
    QQmlExpression e(ctx, v->rootObject(), js);
    e.evaluate();
    if (e.hasError()) fprintf(stderr, "[js-err] %s :: %s\n", qPrintable(js), qPrintable(e.error().toString()));
}

int main(int argc, char **argv) {
    qInstallMessageHandler([](QtMsgType t, const QMessageLogContext &, const QString &m) {
        fprintf(stderr, "[qml:%d] %s\n", t, qPrintable(m)); fflush(stderr);
    });
    QGuiApplication app(argc, argv);
    if (argc < 3) { qWarning() << "usage: harness <Main.qml> <outDir>"; return 2; }
    const QString qml = argv[1], out = argv[2];

    const bool async = qEnvironmentVariableIntValue("KITH_MOCK_ASYNC") == 1;
    MockLogos *logos = async ? new MockLogosAsync(&app) : new MockLogos(&app);
    fprintf(stderr, "[mode] %s bridge\n", async ? "ASYNC (callModuleAsync)" : "SYNC-ONLY (0.2.0, no callModuleAsync)");
    QQuickView view;
    view.rootContext()->setContextProperty("logos", logos);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(1000, 700);
    view.setSource(QUrl::fromLocalFile(qml));
    if (view.status() == QQuickView::Error) {
        for (const auto &e : view.errors()) fprintf(stderr, "[load-err] %s\n", qPrintable(e.toString()));
        return 3;
    }
    view.show();

    // Let Qt.callLater + the first refresh() settle, then select the book (renders the
    // contacts list), screenshot, then walk through each popup.
    QTimer::singleShot(700, [&] { grab(&view, out + "/01-books-empty-select.png"); });
    QTimer::singleShot(900, [&] { runJs(&view, "selectBook('b1')"); });
    QTimer::singleShot(1200, [&] { grab(&view, out + "/02-contacts-list.png"); });
    QTimer::singleShot(1500, [&] { runJs(&view, "openEditContact(contacts[0])"); });
    QTimer::singleShot(1800, [&] { grab(&view, out + "/03-edit-contact-with-identity.png"); runJs(&view, "contactPopup.close()"); });
    QTimer::singleShot(2100, [&] { runJs(&view, "openNewContact()"); });
    QTimer::singleShot(2400, [&] { grab(&view, out + "/04-new-contact.png"); runJs(&view, "contactPopup.close()"); });
    QTimer::singleShot(2700, [&] { runJs(&view, "newBookPopup.open()"); });
    QTimer::singleShot(3000, [&] { grab(&view, out + "/05-new-book-identity-chips.png"); runJs(&view, "newBookPopup.close()"); });
    QTimer::singleShot(3300, [&] { runJs(&view, "importPopup.open()"); });
    QTimer::singleShot(3600, [&] { grab(&view, out + "/06-import-vcard.png"); runJs(&view, "importPopup.close()"); });
    QTimer::singleShot(3900, [&] { runJs(&view, "exportVcard(selectedBookId, '')"); });
    QTimer::singleShot(4200, [&] { grab(&view, out + "/07-export-vcard.png"); runJs(&view, "exportPopup.close()"); });
    QTimer::singleShot(4500, [&] { runJs(&view, "openShare('b1')"); });
    QTimer::singleShot(4800, [&] { grab(&view, out + "/08-share-qr.png"); runJs(&view, "sharePopup.close()"); });
    // Probe state + delete the last book: a READY core's [] must replace the list.
    QTimer::singleShot(5000, [&] { runJs(&view, "console.log('[probe] books=' + books.length + ' contacts=' + contacts.length + ' identities=' + identities.length + ' coreVer=' + coreVer + ' shareLink=' + shareLinkText + ' qr=' + (qrData ? qrData.n : 'null'))"); runJs(&view, "sharePopup.close()"); });
    QTimer::singleShot(5200, [&] { runJs(&view, "deleteBook('b1')"); });
    QTimer::singleShot(6000, [&] { runJs(&view, "console.log('[probe] after delete: books=' + books.length + ' selected=' + JSON.stringify(selectedBookId) + ' refreshing=' + refreshing)"); grab(&view, out + "/09-after-delete.png"); });
    QTimer::singleShot(6300, [&] {
        fprintf(stderr, "[stats] syncCalls=%d", logos->syncCalls);
        if (auto *a = qobject_cast<MockLogosAsync*>(logos)) fprintf(stderr, " asyncCalls=%d maxInFlight=%d", a->asyncCalls, a->maxInFlight);
        fprintf(stderr, "\n");
        app.quit();
    });
    return app.exec();
}
#include "harness.moc"
