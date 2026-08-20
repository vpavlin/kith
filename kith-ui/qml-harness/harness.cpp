// Offscreen render harness for Main.qml (kith_ui). Injects a mock `logos` context object
// (callModule returns canned JSON for "kith" and "loam_core") so the view renders without
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

class MockLogos : public QObject {
    Q_OBJECT
public:
    explicit MockLogos(QObject *p = nullptr) : QObject(p) {}
    bool bookDeleted = false;
    // NOTE: defined OUT OF LINE below — moc's parser chokes on raw string literals inside
    // an inline class body, so keep the class declaration clean.
    Q_INVOKABLE QString callModule(const QString &mod, const QString &method, const QVariant &args);
};

QString MockLogos::callModule(const QString &mod, const QString &method, const QVariant &args) {
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
    if (method == "coreVersion") return "\"0.1.0\"";
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

    auto *logos = new MockLogos(&app);
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
    QTimer::singleShot(3900, [&] { runJs(&view, "exportedVcard = String(j(core('exportVcard', [selectedBookId, '']), '')); exportPopup.open()"); });
    QTimer::singleShot(4200, [&] { grab(&view, out + "/07-export-vcard.png"); runJs(&view, "exportPopup.close()"); });
    QTimer::singleShot(4500, [&] { app.quit(); });
    return app.exec();
}
#include "harness.moc"
