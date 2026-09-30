// Kith — pure-QML Basecamp view over the `kith` core module (kith ADR 0006/0007).
//
// Structural idiom from scala-ui/qml/CalendarView.qml: a j() double/triple-JSON-
// unwrap helper, a poll Timer (events are not reliably delivered to QML), and
// Logos.Theme/Logos.Controls styling (LogosText/LogosButton — the proven-safe
// baseline on the 0.2.0-era bundled design system — plus LogosTextField, which
// scala-ui already uses successfully in its identities popup).
//
// HOUSE RULE: NO BLOCKING CALLS IN QML, ever. Every module call goes through
// call()/core()/loamCore(), which are ASYNC (callback-based): logos.callModuleAsync
// on bridges that have it, else (Basecamp 0.2.0) the sync callModule deferred via
// Qt.callLater — the ONLY remaining callModule call site, inside call().
// refresh() issues at most five calls in parallel (in-flight guarded, never
// overlapping) regardless of how many books/contacts exist:
//   listIdentities + getDefaultIdentityId (loam_core), coreVersion (until known),
//   listBooks()            — core bakes in authorAddr + contactCount per book
//                            (kith_impl.cpp listBooks, from a cached binding).
//   listContacts(bookId)   — the selected book's whole folded contact list.
// Search/filter is client-side JS over that array — zero extra IPC per keystroke.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Logos.Theme
import Logos.Controls

Item {
    id: root
    width: 960
    height: 660

    // ── Kith brand accents (mirror brand/kith-brand.html) ──────────────────────
    // The neutral base stays Logos.Theme (design-system rule); these are mid-tones
    // chosen to read on BOTH Basecamp light and dark, used only for brand moments.
    readonly property color kGilt:     "#AD8020"   // gilt — headers / selection / brand
    readonly property color kUmber:    "#8A6A3F"   // umber — warm structural accent
    readonly property color kSprout:   "#6E9047"   // sprout — the Loam tie (identity)
    readonly property color kRubric:   "#A5392B"   // rubric — warnings
    readonly property color kInk:      "#F1E7CE"   // cream — text on a colored fill
    readonly property color kGiltSoft: Qt.rgba(0.68, 0.50, 0.13, 0.16) // subtle selection wash

    // ── core bridge ──────────────────────────────────────────────────────────
    property bool ready: false
    //   - Newer Basecamp bridges expose callModuleAsync(module, method, args, cb, timeoutMs):
    //     cb receives ONE string (what callModule would return, or {"error":...} incl. timeout).
    //   - Older bridges (Basecamp 0.2.0) lack it: fall back to the sync callModule, but run it
    //     DEFERRED via Qt.callLater (never inside the caller's signal handler) and deliver the
    //     result through the same callback, so every caller is uniformly async.
    // cb is always invoked exactly once, with a string, and always on a LATER event-loop tick
    // (never synchronously inside the caller) — so a result can reassign a Repeater/ListView
    // model without destroying the delegate whose handler started the call.
    readonly property int callTimeoutMs: 30000
    function hasBridge() { return typeof logos !== "undefined" && logos !== null && (typeof logos.callModuleAsync === "function" || typeof logos.callModule === "function") }
    function hasAsyncBridge() { return typeof logos !== "undefined" && logos !== null && typeof logos.callModuleAsync === "function" }
    function _deliver(cb, raw) {
        if (!cb) return
        try { cb(raw === undefined || raw === null ? "" : String(raw)) }
        catch (e) { console.warn("kith view: callback error: " + e) }
    }
    function call(module, method, args, cb) {
        var a = args || []
        if (typeof logos === "undefined" || logos === null) {
            Qt.callLater(function () { root._deliver(cb, '{"error":"no logos bridge"}') })
            return
        }
        if (root.hasAsyncBridge()) {
            try {
                logos.callModuleAsync(module, method, a, function (res) {
                    Qt.callLater(function () { root._deliver(cb, res) })
                }, root.callTimeoutMs)
            } catch (e) {
                var msg = JSON.stringify({ error: "callModuleAsync threw: " + e })
                Qt.callLater(function () { root._deliver(cb, msg) })
            }
            return
        }
        // Fallback (old bridge): the only remaining synchronous callModule, deferred out of the
        // caller's handler.
        Qt.callLater(function () {
            var raw
            try { raw = (typeof logos.callModule === "function") ? logos.callModule(module, method, a) : '{"error":"no callModule"}' }
            catch (e2) { raw = JSON.stringify({ error: "callModule threw: " + e2 }) }
            root._deliver(cb, raw)
        })
    }
    function core(method, args, cb) { root.call("kith", method, args, cb) }
    // Identity SERVICE lives in loam_core (loam ADR 0004 / kith ADR 0003 "Loam owns
    // WHO") — the view talks to it directly for the identity picker, like scala.
    function loamCore(method, args, cb) { root.call("loam_core", method, args, cb) }
    // The bridge's own failure shape: {"error": "..."} (timeout, module missing, threw).
    function errorOf(raw) {
        var v = root.j(raw, null)
        return (v && typeof v === "object" && !Array.isArray(v) && typeof v.error === "string") ? v.error : ""
    }
    // Transient action error (async mutation failed) — shown as a small toast.
    property string actionError: ""
    function reportError(what, raw) {
        var e = root.errorOf(raw)
        if (e === "" && String(raw).trim() !== "") return false
        root.actionError = what + " failed" + (e !== "" ? ": " + e : " (no reply from the kith core)")
        errorToastTimer.restart()
        return true
    }
    Timer { id: errorToastTimer; interval: 6000; onTriggered: root.actionError = "" }
    // Returns can come back double/triple-JSON-encoded through the bridge (scala's
    // documented behaviour) — unwrap up to 3 times.
    function j(raw, fallback) {
        var v = raw
        for (var i = 0; i < 3 && typeof v === "string"; i++) {
            var t = v.trim()
            if (t === "") return fallback
            try { v = JSON.parse(t) } catch (e) { return (i === 0 ? fallback : v) }
        }
        return (v === undefined || v === null) ? fallback : v
    }

    // Core/view version guard (scala's discipline, kith ADR 0006: "same version-skew
    // discipline as Scala"): core + view are separate Basecamp packages.
    property bool coreOutOfDate: false
    property string coreVer: ""
    readonly property string minCore: "0.2.0"
    function verLt(a, b) {
        var pa = String(a).split("."), pb = String(b).split(".")
        for (var i = 0; i < 3; i++) { var x = parseInt(pa[i] || "0"), y = parseInt(pb[i] || "0"); if (x !== y) return x < y }
        return false
    }
    function applyCoreVersion(raw) {
        var v = (root.errorOf(raw) !== "") ? "" : String(raw).trim().replace(/^"|"$/g, "")
        root.coreVer = v
        root.coreOutOfDate = (v === "" || root.verLt(v, root.minCore))
    }

    // ── identities (loam_core service; UI is ours — same shape as scala) ───────
    property var identities: []            // [{id,kind,label,address,pubHex}]
    property string defaultIdentityId: "device"
    // (fetched as part of refresh(); results applied from the async callback)
    function identityLabel(id) {
        for (var i = 0; i < identities.length; i++) if (identities[i].id === id) return identities[i].label
        return id
    }
    function shortAddr(a) {
        if (!a) return ""
        var s = String(a)
        return s.length > 10 ? (s.substring(0, 8) + "…" + s.slice(-4)) : s
    }

    // ── state ────────────────────────────────────────────────────────────────
    property var books: []                 // [{id,name,authorAddr,contactCount}]
    property string selectedBookId: ""
    property var contacts: []              // folded contacts for the selected book
    property string contactSearch: ""

    Component.onCompleted: Qt.callLater(function () {
        root.ready = root.hasBridge()
        if (root.ready) root.refresh()
    })

    Timer {
        interval: 3000; running: true; repeat: true
        onTriggered: root.refresh()
    }

    // Parse a list result, or null when the CALL failed (no core / empty bridge
    // reply / {"error":..} / not JSON / not an array). A legitimately EMPTY list is [].
    function jList(raw) {
        var v = root.j(raw, null)
        return Array.isArray(v) ? v : null
    }
    // Async, batched, never overlapping. A tick that finds a refresh in flight only
    // marks it dirty; the running one re-runs once when it lands. A watchdog (well
    // past the bridge timeout) drops a refresh whose callbacks never came back; its
    // late results are ignored via the generation counter.
    property bool refreshing: false
    property bool refreshDirty: false
    property int refreshGen: 0
    property double refreshStartedAt: 0
    function refresh() {
        if (!root.ready) return
        if (root.refreshing) {
            if (Date.now() - root.refreshStartedAt < root.callTimeoutMs + 5000) { root.refreshDirty = true; return }
            console.warn("kith view: refresh watchdog - dropping a stuck refresh")
        }
        var gen = ++root.refreshGen
        root.refreshing = true; root.refreshDirty = false; root.refreshStartedAt = Date.now()
        var askVersion = (root.coreVer === "")
        var bookId = root.selectedBookId
        var res = {}
        var pending = 0
        function done(key) {
            return function (raw) {
                if (gen !== root.refreshGen) return      // superseded (watchdog)
                res[key] = raw
                if (--pending === 0) root.applyRefresh(res, askVersion, bookId)
            }
        }
        var calls = [["loam_core", "listIdentities", [], "ids"],
                     ["loam_core", "getDefaultIdentityId", [], "def"],
                     ["kith", "listBooks", [], "books"]]
        if (askVersion) calls.push(["kith", "coreVersion", [], "ver"])
        if (bookId !== "") calls.push(["kith", "listContacts", [bookId], "contacts"])
        pending = calls.length
        for (var i = 0; i < calls.length; i++) root.call(calls[i][0], calls[i][1], calls[i][2], done(calls[i][3]))
    }
    function applyRefresh(res, askedVersion, bookId) {
        root.refreshing = false
        var ids = root.jList(res.ids)
        if (ids !== null) root.identities = ids
        var def = root.j(res.def, null)
        if (typeof def === "string" && def !== "") root.defaultIdentityId = def
        // Multi-instance guard (scala's documented basecamp behaviour): a not-yet-loaded
        // core answers nothing (no version, empty/non-JSON/{"error"} reply) - keep what we
        // have then. But a READY core returning [] is the truth (last book/contact
        // deleted) and must replace the list.
        if (askedVersion) root.applyCoreVersion(res.ver)
        var coreUp = root.coreVer !== ""
        var bs = coreUp ? root.jList(res.books) : null
        if (bs !== null) root.books = bs
        // Only judge the selection this refresh was issued for: one started before the
        // user selected (e.g. a just-created book) can't know about it - the dirty
        // re-run will.
        if (root.selectedBookId !== "" && bookId === root.selectedBookId) {
            var stillThere = false
            for (var i = 0; i < root.books.length; i++) if (root.books[i].id === root.selectedBookId) stillThere = true
            if (!stillThere) { root.selectedBookId = ""; root.contacts = [] }
            else if (coreUp && res.contacts !== undefined) {
                var cs = root.jList(res.contacts)
                if (cs !== null) root.contacts = cs
            }
        }
        if (root.refreshDirty) { root.refreshDirty = false; Qt.callLater(root.refresh) }
    }
    function selectBook(id) {
        root.selectedBookId = id
        root.contactSearch = ""
        if (typeof searchField !== "undefined") searchField.text = ""
        root.contacts = []
        root.core("listContacts", [id], function (raw) {
            if (root.selectedBookId !== id) return      // user moved on meanwhile
            var cs = root.jList(raw)
            if (cs !== null) root.contacts = cs
        })
    }
    function createBook(name, identityId) {
        root.core("createBook", [name, identityId], function (raw) {
            if (root.reportError("Creating the book", raw)) { root.refresh(); return }
            var id = String(root.j(raw, ""))
            if (id !== "") root.selectBook(id)
            root.refresh()
        })
    }
    function deleteBook(id) {
        root.core("deleteBook", [id], function (raw) { root.reportError("Deleting the book", raw); root.refresh() })
    }
    function exportVcard(bookId, contactId) {
        root.core("exportVcard", [bookId, contactId || ""], function (raw) {
            var err = root.errorOf(raw)   // an empty export is legit - only a bridge error fails
            if (err !== "") { root.reportError("Exporting", raw); return }
            root.exportedVcard = String(root.j(raw, ""))
            exportPopup.open()
        })
    }
    function bookById(id) {
        for (var i = 0; i < books.length; i++) if (books[i].id === id) return books[i]
        return null
    }
    function contactName(c) {
        return (c && c.name && c.name.display) ? c.name.display : "(unnamed)"
    }
    function contactSecondary(c) {
        if (c.emails && c.emails.length > 0) return c.emails[0].value
        if (c.phones && c.phones.length > 0) return c.phones[0].value
        return ""
    }
    function hasIdentity(c) { return !!(c && c.loamIdentity && c.loamIdentity.address) }
    function contactsFiltered() {
        var q = contactSearch.trim().toLowerCase()
        if (q === "") return contacts
        var out = []
        for (var i = 0; i < contacts.length; i++) {
            var c = contacts[i]
            var hay = contactName(c) + " " + (c.notes || "")
            if (c.emails) for (var e = 0; e < c.emails.length; e++) hay += " " + c.emails[e].value
            if (c.phones) for (var p = 0; p < c.phones.length; p++) hay += " " + c.phones[p].value
            if (c.handles) for (var h = 0; h < c.handles.length; h++) hay += " " + c.handles[h].value
            if (hay.toLowerCase().indexOf(q) !== -1) out.push(c)
        }
        return out
    }

    // ── generic confirm popup (delete book / delete contact) ───────────────────
    property string confirmText: ""
    property var confirmAction: null
    function askConfirm(text, fn) { root.confirmText = text; root.confirmAction = fn; confirmPopup.open() }
    Popup {
        id: confirmPopup
        anchors.centerIn: Overlay.overlay
        width: 360; modal: true; padding: Theme.spacing.large
        background: Rectangle { radius: Theme.spacing.radiusMedium; color: Theme.palette.backgroundElevated; border.width: 1; border.color: Theme.palette.borderHairline }
        ColumnLayout {
            anchors.fill: parent; spacing: Theme.spacing.medium
            LogosText { text: root.confirmText; color: Theme.palette.text; font.pixelSize: 14; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true; spacing: Theme.spacing.small
                Item { Layout.fillWidth: true }
                LogosButton { text: "Cancel"; onClicked: confirmPopup.close() }
                LogosButton {
                    text: "Delete"
                    onClicked: { if (root.confirmAction) root.confirmAction(); confirmPopup.close() }
                }
            }
        }
    }

    // ── layout ───────────────────────────────────────────────────────────────
    Rectangle { anchors.fill: parent; color: Theme.palette.background }

    Rectangle {
        id: staleCoreBanner
        visible: root.coreOutOfDate
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: visible ? bannerCol.implicitHeight + 18 : 0
        z: 9999
        color: root.kRubric
        ColumnLayout {
            id: bannerCol
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 16; rightMargin: 16 }
            spacing: 1
            LogosText { text: "⚠  Kith core is out of date" + (root.coreVer ? " (v" + root.coreVer + ")" : ""); color: root.kInk; font.pixelSize: 14 }
            LogosText { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "Update the 'kith' package to " + root.minCore + "+ in Basecamp."; color: root.kInk; font.pixelSize: 12 }
        }
    }

    SplitView {
        anchors.top: staleCoreBanner.bottom; anchors.left: parent.left
        anchors.right: parent.right; anchors.bottom: parent.bottom
        orientation: Qt.Horizontal

        // ── pane 1: books sidebar ──────────────────────────────────────────
        Rectangle {
            SplitView.preferredWidth: 240
            SplitView.minimumWidth: 180
            color: Theme.palette.backgroundInset
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacing.medium
                spacing: Theme.spacing.small

                LogosText { text: "Books"; color: root.kGilt; font.pixelSize: 18; font.weight: Theme.typography.weightMedium }

                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                    model: root.books
                    spacing: 2
                    delegate: Rectangle {
                        width: ListView.view.width; height: 44
                        radius: Theme.spacing.radiusSmall
                        color: root.selectedBookId === modelData.id ? root.kGiltSoft : "transparent"
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: Theme.spacing.small; anchors.rightMargin: Theme.spacing.small; spacing: Theme.spacing.small
                            ColumnLayout {
                                Layout.fillWidth: true; Layout.alignment: Qt.AlignVCenter; spacing: 1
                                RowLayout {
                                    Layout.fillWidth: true; spacing: 4
                                    LogosText { text: modelData.name || "(unnamed)"; color: Theme.palette.text; font.pixelSize: 14; Layout.fillWidth: true; elide: Text.ElideRight }
                                    LogosText { visible: !!modelData.syncing; text: "🔄"; font.pixelSize: 11 }
                                }
                                LogosText {
                                    text: modelData.contactCount + " contact" + (modelData.contactCount === 1 ? "" : "s")
                                          + (modelData.authorAddr ? "  ·  " + root.shortAddr(modelData.authorAddr) : "")
                                    color: Theme.palette.textTertiary; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight
                                }
                            }
                            LogosText {
                                text: "✕"; color: Theme.palette.textTertiary; font.pixelSize: 14; Layout.alignment: Qt.AlignVCenter
                                MouseArea {
                                    anchors.fill: parent; anchors.margins: -4
                                    onClicked: {
                                        var bid = modelData.id
                                        root.askConfirm(
                                            "Delete \"" + (modelData.name || "this book") + "\" and its " + modelData.contactCount + " contact(s)? This can't be undone.",
                                            function () { root.deleteBook(bid) })
                                    }
                                }
                            }
                        }
                        MouseArea { anchors.fill: parent; z: -1; onClicked: root.selectBook(modelData.id) }
                    }
                }

                LogosText {
                    visible: root.books.length === 0
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    text: "No books yet. Create one to start adding contacts."
                    color: Theme.palette.textTertiary; font.pixelSize: 12
                }

                RowLayout {
                    Layout.fillWidth: true; spacing: Theme.spacing.small
                    LogosButton { Layout.fillWidth: true; text: "+ New book"; onClicked: newBookPopup.open() }
                    LogosButton { Layout.fillWidth: true; text: "Join…"; onClicked: joinBookPopup.open() }
                }
            }
        }

        // ── pane 2: contacts ─────────────────────────────────────────────────
        // A plain Item (not a Layout) so the two stacked children below can use
        // anchors freely — putting them directly under a ColumnLayout produced
        // "anchors on an item managed by a layout" warnings (caught by the qml-harness
        // offscreen render, see qml-harness/).
        Item {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 420

            // No book selected → placeholder.
            ColumnLayout {
                visible: root.selectedBookId === ""
                anchors.centerIn: parent
                spacing: Theme.spacing.small
                LogosText { text: "Select or create a book"; color: Theme.palette.textTertiary; font.pixelSize: 16; Layout.alignment: Qt.AlignHCenter }
            }

            ColumnLayout {
                visible: root.selectedBookId !== ""
                anchors.fill: parent
                anchors.margins: Theme.spacing.medium
                spacing: Theme.spacing.small

                RowLayout {
                    Layout.fillWidth: true; spacing: Theme.spacing.small
                    LogosText {
                        text: root.bookById(root.selectedBookId) ? root.bookById(root.selectedBookId).name : ""
                        color: Theme.palette.text; font.pixelSize: 20; font.weight: Theme.typography.weightMedium
                        Layout.fillWidth: true; elide: Text.ElideRight
                    }
                    LogosButton {
                        text: "Share…"
                        onClicked: { root.openShare(root.selectedBookId) }
                    }
                    LogosButton { text: "Import vCard"; onClicked: importPopup.open() }
                    LogosButton {
                        text: "Export book"
                        onClicked: root.exportVcard(root.selectedBookId, "")
                    }
                    LogosButton { text: "+ Add contact"; onClicked: root.openNewContact() }
                }

                Field {
                    id: searchField
                    Layout.fillWidth: true
                    placeholderText: "Search this book (name, email, phone, handle)…"
                    onTextChanged: root.contactSearch = text
                }

                ListView {
                    id: contactList
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                    model: root.contactsFiltered()
                    spacing: 2
                    delegate: Rectangle {
                        width: ListView.view.width; height: 52
                        radius: Theme.spacing.radiusSmall
                        color: Theme.palette.backgroundInset
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: Theme.spacing.small; anchors.rightMargin: Theme.spacing.small; spacing: Theme.spacing.small
                            LogosText {
                                text: root.hasIdentity(modelData) ? "🔑" : "👤"
                                font.pixelSize: 16; Layout.alignment: Qt.AlignVCenter
                            }
                            ColumnLayout {
                                Layout.fillWidth: true; Layout.alignment: Qt.AlignVCenter; spacing: 1
                                LogosText { text: root.contactName(modelData); color: Theme.palette.text; font.pixelSize: 14; Layout.fillWidth: true; elide: Text.ElideRight }
                                LogosText {
                                    text: root.contactSecondary(modelData)
                                    visible: text.length > 0
                                    color: Theme.palette.textTertiary; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight
                                }
                            }
                        }
                        MouseArea { anchors.fill: parent; onClicked: root.openEditContact(modelData) }
                    }
                }

                LogosText {
                    visible: root.contactsFiltered().length === 0
                    Layout.alignment: Qt.AlignHCenter
                    text: root.contacts.length === 0 ? "No contacts in this book yet." : "No contacts match your search."
                    color: Theme.palette.textTertiary; font.pixelSize: 13
                }
            }
        }
    }

    // ── async action error toast (a mutation's reply was an error / nothing) ──
    Rectangle {
        visible: root.actionError !== ""
        z: 9998
        anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter; bottomMargin: 16 }
        width: Math.min(root.width - 32, errText.implicitWidth + 32); height: errText.implicitHeight + 16
        radius: Theme.spacing.radiusSmall; color: root.kRubric
        LogosText { id: errText; anchors.centerIn: parent; width: parent.width - 32; wrapMode: Text.WordWrap; text: root.actionError; color: root.kInk; font.pixelSize: 12 }
        MouseArea { anchors.fill: parent; onClicked: root.actionError = "" }
    }

    // ── new-book popup: name + "Author as" identity chips (kith ADR 0003) ──────
    property string newBookIdentity: ""
    Popup {
        id: newBookPopup
        anchors.centerIn: Overlay.overlay
        width: 420; modal: true; padding: Theme.spacing.large
        background: Rectangle { radius: Theme.spacing.radiusMedium; color: Theme.palette.backgroundElevated; border.width: 1; border.color: Theme.palette.borderHairline }
        onOpened: { newBookName.text = ""; root.newBookIdentity = "" }
        ColumnLayout {
            anchors.fill: parent; spacing: Theme.spacing.small
            LogosText { text: "New book"; color: Theme.palette.text; font.pixelSize: 18; font.weight: Theme.typography.weightMedium }

            LogosText { text: "Name"; color: Theme.palette.textTertiary; font.pixelSize: 11 }
            Field { id: newBookName; Layout.fillWidth: true; placeholderText: "e.g. Personal, Household, Work" }

            LogosText { text: "Author as"; color: Theme.palette.textTertiary; font.pixelSize: 11 }
            Flow {
                Layout.fillWidth: true; spacing: Theme.spacing.small
                Repeater {
                    model: root.identities
                    Rectangle {
                        radius: Theme.spacing.radiusSmall
                        readonly property bool chipSel: (root.newBookIdentity === modelData.id || (root.newBookIdentity === "" && modelData.id === root.defaultIdentityId))
                        color: chipSel ? root.kGilt : Theme.palette.backgroundSecondary
                        border.width: 1
                        border.color: chipSel ? root.kGilt : Theme.palette.borderHairline
                        implicitHeight: nbChipT.implicitHeight + 10; implicitWidth: nbChipT.implicitWidth + 22
                        LogosText { id: nbChipT; anchors.centerIn: parent; text: modelData.label; font.pixelSize: 12; color: parent.chipSel ? "#2A2118" : Theme.palette.text }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.newBookIdentity = modelData.id }
                    }
                }
            }
            LogosText { text: "This identity owns the book and signs its contact edits."; color: Theme.palette.textTertiary; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }

            RowLayout {
                Layout.fillWidth: true; Layout.topMargin: Theme.spacing.small; spacing: Theme.spacing.small
                Item { Layout.fillWidth: true }
                LogosButton { text: "Cancel"; onClicked: newBookPopup.close() }
                LogosButton {
                    text: "Create"; enabled: newBookName.text.trim().length > 0
                    onClicked: {
                        root.createBook(newBookName.text.trim(), root.newBookIdentity || root.defaultIdentityId)
                        newBookPopup.close()
                    }
                }
            }
        }
    }

    // ── contact detail / add / edit popup ───────────────────────────────────
    property var editingContact: null    // null = adding a new contact
    ListModel { id: phonesModel }
    ListModel { id: emailsModel }
    ListModel { id: handlesModel }
    ListModel { id: addressesModel }
    property bool cHasIdentity: false
    property string cIdentityAddr: ""
    property string cIdentityPub: ""
    property bool cIdentityVerified: false
    property string cIdentityAddedVia: "manual"

    function openNewContact() {
        root.editingContact = null
        cName.text = ""; cGiven.text = ""; cFamily.text = ""; cOrg.text = ""; cNotes.text = ""
        phonesModel.clear(); emailsModel.clear(); handlesModel.clear(); addressesModel.clear()
        root.cHasIdentity = false; root.cIdentityAddr = ""; root.cIdentityPub = ""
        root.cIdentityVerified = false; root.cIdentityAddedVia = "manual"
        cIdAddrField.text = ""; cIdPubField.text = ""
        contactPopup.open()
    }
    function openEditContact(c) {
        root.editingContact = c
        var n = c.name || {}
        cName.text = n.display || ""; cGiven.text = n.given || ""; cFamily.text = n.family || ""; cOrg.text = n.org || ""
        cNotes.text = c.notes || ""
        phonesModel.clear(); (c.phones || []).forEach(function (p) { phonesModel.append({ label: p.label || "mobile", value: p.value || "" }) })
        emailsModel.clear(); (c.emails || []).forEach(function (e) { emailsModel.append({ label: e.label || "home", value: e.value || "" }) })
        handlesModel.clear(); (c.handles || []).forEach(function (h) { handlesModel.append({ kind: h.kind || "telegram", value: h.value || "" }) })
        addressesModel.clear(); (c.addresses || []).forEach(function (a) {
            addressesModel.append({ label: a.label || "home", street: a.street || "", city: a.city || "", region: a.region || "", postcode: a.postcode || "", country: a.country || "" })
        })
        var li = c.loamIdentity
        root.cHasIdentity = !!(li && li.address)
        root.cIdentityAddr = li ? (li.address || "") : ""
        root.cIdentityPub = li ? (li.pubHex || "") : ""
        root.cIdentityVerified = li ? !!li.verified : false
        root.cIdentityAddedVia = li ? (li.addedVia || "manual") : "manual"
        cIdAddrField.text = root.cIdentityAddr; cIdPubField.text = root.cIdentityPub
        contactPopup.open()
    }
    function buildContactPayload() {
        var p = {
            name: { display: cName.text.trim(), given: cGiven.text.trim(), family: cFamily.text.trim(), org: cOrg.text.trim() },
            phones: [], emails: [], handles: [], addresses: [],
            notes: cNotes.text.trim()
        }
        for (var i = 0; i < phonesModel.count; i++) { var ph = phonesModel.get(i); if (ph.value.trim() !== "") p.phones.push({ label: ph.label, value: ph.value.trim() }) }
        for (var j2 = 0; j2 < emailsModel.count; j2++) { var em = emailsModel.get(j2); if (em.value.trim() !== "") p.emails.push({ label: em.label, value: em.value.trim() }) }
        for (var k = 0; k < handlesModel.count; k++) { var hd = handlesModel.get(k); if (hd.value.trim() !== "") p.handles.push({ kind: hd.kind, value: hd.value.trim() }) }
        for (var a = 0; a < addressesModel.count; a++) {
            var ad = addressesModel.get(a)
            if (ad.street.trim() !== "" || ad.city.trim() !== "")
                p.addresses.push({ label: ad.label, street: ad.street.trim(), city: ad.city.trim(), region: ad.region.trim(), postcode: ad.postcode.trim(), country: ad.country.trim() })
        }
        if (root.cHasIdentity && root.cIdentityAddr.trim() !== "") {
            p.loamIdentity = { address: root.cIdentityAddr.trim(), pubHex: root.cIdentityPub.trim(), verified: root.cIdentityVerified, addedVia: root.cIdentityAddedVia }
        }
        if (root.editingContact) p.id = root.editingContact.id
        return p
    }
    function saveContact() {
        var p = root.buildContactPayload()
        var editing = !!root.editingContact
        root.core(editing ? "editContact" : "addContact", [root.selectedBookId, JSON.stringify(p)], function (raw) {
            root.reportError(editing ? "Saving the contact" : "Adding the contact", raw)
            root.refresh()
        })
        contactPopup.close()
    }
    function deleteContact() {
        if (root.editingContact) {
            var id = root.editingContact.id
            var bookId = root.selectedBookId
            contactPopup.close()
            root.askConfirm("Delete " + root.contactName(root.editingContact) + "?", function () {
                root.core("deleteContact", [bookId, id], function (raw) { root.reportError("Deleting the contact", raw); root.refresh() })
            })
        }
    }

    readonly property var phoneLabels: ["mobile", "home", "work", "other"]
    readonly property var handleKinds: ["telegram", "signal", "matrix", "xmpp", "other"]

    Popup {
        id: contactPopup
        anchors.centerIn: Overlay.overlay
        width: Math.min(600, root.width - 40); height: Math.min(620, root.height - 40); modal: true; padding: Theme.spacing.large
        background: Rectangle { radius: Theme.spacing.radiusMedium; color: Theme.palette.backgroundElevated; border.width: 1; border.color: Theme.palette.borderHairline }

        ColumnLayout {
            anchors.fill: parent; spacing: Theme.spacing.small
            LogosText { text: root.editingContact ? "Edit contact" : "New contact"; color: Theme.palette.text; font.pixelSize: 18; font.weight: Theme.typography.weightMedium }

            Flickable {
                Layout.fillWidth: true; Layout.fillHeight: true
                contentWidth: width; contentHeight: contactBody.implicitHeight
                clip: true
                ScrollBar.vertical: ScrollBar {}
                ColumnLayout {
                    id: contactBody
                    width: parent.width; spacing: Theme.spacing.small

                    LogosText { text: "Display name"; color: Theme.palette.textTertiary; font.pixelSize: 11 }
                    Field { id: cName; Layout.fillWidth: true; placeholderText: "Full name" }
                    RowLayout {
                        Layout.fillWidth: true; spacing: Theme.spacing.small
                        ColumnLayout { Layout.fillWidth: true; LogosText { text: "Given"; color: Theme.palette.textTertiary; font.pixelSize: 11 } Field { id: cGiven; Layout.fillWidth: true } }
                        ColumnLayout { Layout.fillWidth: true; LogosText { text: "Family"; color: Theme.palette.textTertiary; font.pixelSize: 11 } Field { id: cFamily; Layout.fillWidth: true } }
                    }
                    LogosText { text: "Organization"; color: Theme.palette.textTertiary; font.pixelSize: 11 }
                    Field { id: cOrg; Layout.fillWidth: true; placeholderText: "Optional" }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.palette.borderHairline; Layout.topMargin: 4 }

                    // ── phones ──
                    LogosText { text: "Phones"; color: Theme.palette.text; font.pixelSize: 13; font.weight: Theme.typography.weightMedium }
                    Repeater {
                        model: phonesModel
                        delegate: RowLayout {
                            Layout.fillWidth: true; spacing: Theme.spacing.small
                            ComboBox { Layout.preferredWidth: 100; model: root.phoneLabels; currentIndex: root.phoneLabels.indexOf(model.label); onActivated: function (i) { phonesModel.setProperty(index, "label", root.phoneLabels[i]) } }
                            Field { Layout.fillWidth: true; placeholderText: "phone number"; text: model.value; onTextChanged: phonesModel.setProperty(index, "value", text) }
                            LogosText { text: "✕"; color: Theme.palette.textTertiary; font.pixelSize: 14; MouseArea { anchors.fill: parent; anchors.margins: -4; onClicked: phonesModel.remove(index) } }
                        }
                    }
                    LogosButton { text: "+ Add phone"; onClicked: phonesModel.append({ label: "mobile", value: "" }) }

                    // ── emails ──
                    LogosText { text: "Emails"; color: Theme.palette.text; font.pixelSize: 13; font.weight: Theme.typography.weightMedium; Layout.topMargin: 4 }
                    Repeater {
                        model: emailsModel
                        delegate: RowLayout {
                            Layout.fillWidth: true; spacing: Theme.spacing.small
                            ComboBox { Layout.preferredWidth: 100; model: root.phoneLabels; currentIndex: root.phoneLabels.indexOf(model.label); onActivated: function (i) { emailsModel.setProperty(index, "label", root.phoneLabels[i]) } }
                            Field { Layout.fillWidth: true; placeholderText: "email address"; text: model.value; onTextChanged: emailsModel.setProperty(index, "value", text) }
                            LogosText { text: "✕"; color: Theme.palette.textTertiary; font.pixelSize: 14; MouseArea { anchors.fill: parent; anchors.margins: -4; onClicked: emailsModel.remove(index) } }
                        }
                    }
                    LogosButton { text: "+ Add email"; onClicked: emailsModel.append({ label: "home", value: "" }) }

                    // ── handles ──
                    LogosText { text: "Messaging handles"; color: Theme.palette.text; font.pixelSize: 13; font.weight: Theme.typography.weightMedium; Layout.topMargin: 4 }
                    Repeater {
                        model: handlesModel
                        delegate: RowLayout {
                            Layout.fillWidth: true; spacing: Theme.spacing.small
                            ComboBox { Layout.preferredWidth: 100; model: root.handleKinds; currentIndex: root.handleKinds.indexOf(model.kind); onActivated: function (i) { handlesModel.setProperty(index, "kind", root.handleKinds[i]) } }
                            Field { Layout.fillWidth: true; placeholderText: "handle / username"; text: model.value; onTextChanged: handlesModel.setProperty(index, "value", text) }
                            LogosText { text: "✕"; color: Theme.palette.textTertiary; font.pixelSize: 14; MouseArea { anchors.fill: parent; anchors.margins: -4; onClicked: handlesModel.remove(index) } }
                        }
                    }
                    LogosButton { text: "+ Add handle"; onClicked: handlesModel.append({ kind: "telegram", value: "" }) }

                    // ── addresses ──
                    LogosText { text: "Addresses"; color: Theme.palette.text; font.pixelSize: 13; font.weight: Theme.typography.weightMedium; Layout.topMargin: 4 }
                    Repeater {
                        model: addressesModel
                        delegate: ColumnLayout {
                            Layout.fillWidth: true; spacing: 2
                            RowLayout {
                                Layout.fillWidth: true; spacing: Theme.spacing.small
                                ComboBox { Layout.preferredWidth: 100; model: root.phoneLabels; currentIndex: root.phoneLabels.indexOf(model.label); onActivated: function (i) { addressesModel.setProperty(index, "label", root.phoneLabels[i]) } }
                                Field { Layout.fillWidth: true; placeholderText: "street"; text: model.street; onTextChanged: addressesModel.setProperty(index, "street", text) }
                                LogosText { text: "✕"; color: Theme.palette.textTertiary; font.pixelSize: 14; MouseArea { anchors.fill: parent; anchors.margins: -4; onClicked: addressesModel.remove(index) } }
                            }
                            RowLayout {
                                Layout.fillWidth: true; spacing: Theme.spacing.small
                                Field { Layout.fillWidth: true; placeholderText: "city"; text: model.city; onTextChanged: addressesModel.setProperty(index, "city", text) }
                                Field { Layout.fillWidth: true; placeholderText: "region"; text: model.region; onTextChanged: addressesModel.setProperty(index, "region", text) }
                                Field { Layout.preferredWidth: 90; placeholderText: "postcode"; text: model.postcode; onTextChanged: addressesModel.setProperty(index, "postcode", text) }
                                Field { Layout.preferredWidth: 90; placeholderText: "country"; text: model.country; onTextChanged: addressesModel.setProperty(index, "country", text) }
                            }
                        }
                    }
                    LogosButton { text: "+ Add address"; onClicked: addressesModel.append({ label: "home", street: "", city: "", region: "", postcode: "", country: "" }) }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.palette.borderHairline; Layout.topMargin: 4 }

                    LogosText { text: "Notes"; color: Theme.palette.textTertiary; font.pixelSize: 11 }
                    // Plain multi-line TextArea (not gated by the design system), themed with tokens.
                    TextArea {
                        id: cNotes
                        Layout.fillWidth: true; Layout.preferredHeight: 60
                        wrapMode: TextArea.Wrap; selectByMouse: true
                        color: Theme.palette.text; font.pixelSize: 13
                        background: Rectangle { radius: Theme.spacing.radiusSmall; color: Theme.palette.background; border.width: 1; border.color: Theme.palette.borderHairline }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.palette.borderHairline; Layout.topMargin: 4 }

                    // ── Loam identity (kith ADR 0002/0003) — the optional cryptographic principal ──
                    RowLayout {
                        Layout.fillWidth: true
                        LogosText { text: "🔑 Loam identity"; color: Theme.palette.text; font.pixelSize: 13; font.weight: Theme.typography.weightMedium; Layout.fillWidth: true }
                        LogosButton {
                            text: root.cHasIdentity ? "Remove" : "+ Add"
                            onClicked: {
                                root.cHasIdentity = !root.cHasIdentity
                                if (!root.cHasIdentity) { root.cIdentityAddr = ""; root.cIdentityPub = ""; cIdAddrField.text = ""; cIdPubField.text = "" }
                            }
                        }
                    }
                    LogosText {
                        visible: !root.cHasIdentity
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                        text: "No identity — this contact is a human reference only (name/phone/email), not a grantable principal in other apps."
                        color: Theme.palette.textTertiary; font.pixelSize: 11
                    }
                    ColumnLayout {
                        visible: root.cHasIdentity
                        Layout.fillWidth: true; spacing: 2
                        LogosText { text: "Address"; color: Theme.palette.textTertiary; font.pixelSize: 11 }
                        Field { id: cIdAddrField; Layout.fillWidth: true; placeholderText: "0x…"; onTextChanged: root.cIdentityAddr = text }
                        LogosText { text: "Public key (hex, optional)"; color: Theme.palette.textTertiary; font.pixelSize: 11 }
                        Field { id: cIdPubField; Layout.fillWidth: true; placeholderText: "hex pubkey"; onTextChanged: root.cIdentityPub = text }
                        RowLayout {
                            spacing: 8
                            Rectangle {
                                width: 20; height: 20; radius: 5
                                color: root.cIdentityVerified ? Theme.palette.primary : Theme.palette.background
                                border.width: 1; border.color: Theme.palette.borderHairline
                                LogosText { anchors.centerIn: parent; visible: root.cIdentityVerified; text: "✓"; color: Theme.palette.background; font.pixelSize: 13 }
                                MouseArea { anchors.fill: parent; onClicked: root.cIdentityVerified = !root.cIdentityVerified }
                            }
                            LogosText { text: "Verified"; color: Theme.palette.text; font.pixelSize: 12 }
                            LogosText { text: "  ·  added via " + root.cIdentityAddedVia; color: Theme.palette.textTertiary; font.pixelSize: 11 }
                        }
                    }

                    // ── read-only metadata on an existing contact ──
                    ColumnLayout {
                        visible: root.editingContact !== null
                        Layout.fillWidth: true; Layout.topMargin: 4; spacing: 1
                        LogosText {
                            visible: root.editingContact && root.editingContact.authorAddr
                            text: "Authored by " + (root.editingContact ? root.shortAddr(root.editingContact.authorAddr) : "")
                            color: Theme.palette.textTertiary; font.pixelSize: 10
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true; Layout.topMargin: Theme.spacing.small; spacing: Theme.spacing.small
                LogosButton { visible: root.editingContact !== null; text: "Delete"; onClicked: root.deleteContact() }
                LogosButton {
                    visible: root.editingContact !== null; text: "Export vCard"
                    onClicked: root.exportVcard(root.selectedBookId, root.editingContact.id)
                }
                Item { Layout.fillWidth: true }
                LogosButton { text: "Cancel"; onClicked: contactPopup.close() }
                LogosButton { text: root.editingContact ? "Save" : "Add"; enabled: cName.text.trim().length > 0; onClicked: root.saveContact() }
            }
        }
    }

    // ── import vCard popup ──────────────────────────────────────────────────
    property string importResult: ""
    property bool importing: false
    Popup {
        id: importPopup
        anchors.centerIn: Overlay.overlay
        width: 460; modal: true; padding: Theme.spacing.large
        background: Rectangle { radius: Theme.spacing.radiusMedium; color: Theme.palette.backgroundElevated; border.width: 1; border.color: Theme.palette.borderHairline }
        onOpened: { vcardInput.text = ""; root.importResult = "" }
        ColumnLayout {
            anchors.fill: parent; spacing: Theme.spacing.small
            LogosText { text: "Import vCard"; color: Theme.palette.text; font.pixelSize: 18; font.weight: Theme.typography.weightMedium }
            LogosText { text: "Paste one or more vCard 4.0 entries (.vcf text) below."; color: Theme.palette.textTertiary; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            TextArea {
                id: vcardInput
                Layout.fillWidth: true; Layout.preferredHeight: 220
                wrapMode: TextArea.Wrap; selectByMouse: true
                font.family: "monospace"; font.pixelSize: 12; color: Theme.palette.text
                background: Rectangle { radius: Theme.spacing.radiusSmall; color: Theme.palette.background; border.width: 1; border.color: Theme.palette.borderHairline }
            }
            LogosText { visible: root.importResult !== ""; text: root.importResult; color: Theme.palette.primary; font.pixelSize: 12 }
            RowLayout {
                Layout.fillWidth: true; spacing: Theme.spacing.small
                Item { Layout.fillWidth: true }
                LogosButton { text: "Close"; onClicked: importPopup.close() }
                LogosButton {
                    text: root.importing ? "Importing…" : "Import"
                    enabled: !root.importing && vcardInput.text.trim().length > 0
                    onClicked: {
                        root.importing = true
                        root.importResult = ""
                        root.core("importVcard", [root.selectedBookId, vcardInput.text], function (raw) {
                            root.importing = false
                            var err = root.errorOf(raw)
                            if (err !== "") root.importResult = "Import failed: " + err
                            else {
                                var res = root.j(raw, { imported: 0 })
                                root.importResult = "Imported " + ((res && res.imported) || 0) + " contact(s)."
                            }
                            root.refresh()
                        })
                    }
                }
            }
        }
    }

    // ── share popup (kith Phase 4: sync) — copyable kith://join… link + a real
    //    QR the phone can scan (mirrors scala's CalendarView share popup) ───────
    property string shareLinkText: ""
    property var qrData: null    // { n, cells } from core qrMatrix
    TextEdit { id: shareClipHelper; visible: false; text: root.shareLinkText }
    // Open immediately; the link then the QR fill in as the async replies land. A
    // newer openShare supersedes an older one's late replies (shareSeq).
    property int shareSeq: 0
    function openShare(bookId) {
        var seq = ++root.shareSeq
        root.shareLinkText = ""
        root.qrData = null
        qrCanvas.requestPaint()
        sharePopup.open()
        root.core("shareLink", [bookId], function (raw) {
            if (seq !== root.shareSeq) return
            if (root.reportError("Building the share link", raw)) return
            root.shareLinkText = String(root.j(raw, ""))
            if (root.shareLinkText === "") return
            // Build a scannable QR matrix from the core (drawn on a Canvas; data: URIs
            // are blocked in the sandbox, so we render cells ourselves).
            root.core("qrMatrix", [root.shareLinkText], function (qraw) {
                if (seq !== root.shareSeq) return
                var m = root.j(qraw, null)
                if (m && m.ok && m.n && m.cells && m.cells.length >= m.n * m.n) root.qrData = { n: m.n, cells: m.cells }
                qrCanvas.requestPaint()
            })
        })
    }
    Popup {
        id: sharePopup
        anchors.centerIn: Overlay.overlay
        width: 460; modal: true; padding: Theme.spacing.large
        background: Rectangle { radius: Theme.spacing.radiusMedium; color: Theme.palette.backgroundElevated; border.width: 1; border.color: Theme.palette.borderHairline }
        onOpened: qrCanvas.requestPaint()
        ColumnLayout {
            anchors.fill: parent; spacing: Theme.spacing.small
            LogosText { text: "Share this book"; color: Theme.palette.text; font.pixelSize: 18; font.weight: Theme.typography.weightMedium }
            LogosText {
                text: "Anyone with this link can join and sync this book's contacts. Scan on the phone, or send the link over a trusted channel."
                color: Theme.palette.textTertiary; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                width: 220; height: 220; radius: Theme.spacing.radiusSmall; color: "#ffffff"
                visible: root.qrData !== null
                Canvas {
                    id: qrCanvas; anchors.fill: parent; anchors.margins: 10
                    onPaint: {
                        var ctx = getContext("2d"); ctx.reset()
                        ctx.fillStyle = "#ffffff"; ctx.fillRect(0, 0, width, height)
                        var d = root.qrData; if (!d || !d.n) return
                        var cell = width / d.n; ctx.fillStyle = "#000000"
                        for (var y = 0; y < d.n; y++)
                            for (var x = 0; x < d.n; x++)
                                if (d.cells[y * d.n + x])
                                    ctx.fillRect(Math.floor(x * cell), Math.floor(y * cell), Math.ceil(cell), Math.ceil(cell))
                    }
                }
            }
            Field { id: shareLinkField; Layout.fillWidth: true; readOnly: true; text: root.shareLinkText }
            RowLayout {
                Layout.fillWidth: true; spacing: Theme.spacing.small
                LogosButton {
                    text: "Copy"
                    onClicked: { shareClipHelper.text = root.shareLinkText; shareClipHelper.selectAll(); shareClipHelper.copy() }
                }
                Item { Layout.fillWidth: true }
                LogosButton { text: "Close"; onClicked: sharePopup.close() }
            }
        }
    }

    // ── join popup (kith Phase 4: sync) — paste a kith://join… link ────────────
    property string joinResult: ""
    property bool joining: false
    Popup {
        id: joinBookPopup
        anchors.centerIn: Overlay.overlay
        width: 460; modal: true; padding: Theme.spacing.large
        background: Rectangle { radius: Theme.spacing.radiusMedium; color: Theme.palette.backgroundElevated; border.width: 1; border.color: Theme.palette.borderHairline }
        onOpened: { joinLinkField.text = ""; root.joinResult = "" }
        ColumnLayout {
            anchors.fill: parent; spacing: Theme.spacing.small
            LogosText { text: "Join a shared book"; color: Theme.palette.text; font.pixelSize: 18; font.weight: Theme.typography.weightMedium }
            LogosText { text: "Paste a kith://join… link below."; color: Theme.palette.textTertiary; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Field { id: joinLinkField; Layout.fillWidth: true; placeholderText: "kith://join?id=…&key=…&name=…" }
            LogosText { visible: root.joinResult !== ""; text: root.joinResult; color: Theme.palette.primary; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true; spacing: Theme.spacing.small
                Item { Layout.fillWidth: true }
                LogosButton { text: "Close"; onClicked: joinBookPopup.close() }
                LogosButton {
                    text: root.joining ? "Joining…" : "Join"
                    enabled: !root.joining && joinLinkField.text.trim().length > 0
                    onClicked: {
                        root.joining = true
                        root.joinResult = ""
                        root.core("handleShareLink", [joinLinkField.text.trim(), root.defaultIdentityId], function (raw) {
                            root.joining = false
                            var err = root.errorOf(raw)
                            var ok = root.j(raw, false)
                            if (ok === true || ok === "true") {
                                root.joinResult = "Joined — syncing now."
                                root.refresh()
                            } else if (err !== "") {
                                root.joinResult = "Couldn't join: " + err
                            } else {
                                root.joinResult = "Couldn't parse that link."
                            }
                        })
                    }
                }
            }
        }
    }

    // ── export vCard popup (read-only, selectable + a Copy button) ─────────
    property string exportedVcard: ""
    TextEdit { id: clipHelper; visible: false; text: root.exportedVcard }
    Popup {
        id: exportPopup
        anchors.centerIn: Overlay.overlay
        width: 460; modal: true; padding: Theme.spacing.large
        background: Rectangle { radius: Theme.spacing.radiusMedium; color: Theme.palette.backgroundElevated; border.width: 1; border.color: Theme.palette.borderHairline }
        ColumnLayout {
            anchors.fill: parent; spacing: Theme.spacing.small
            LogosText { text: "Exported vCard"; color: Theme.palette.text; font.pixelSize: 18; font.weight: Theme.typography.weightMedium }
            TextArea {
                id: vcardOutput
                Layout.fillWidth: true; Layout.preferredHeight: 260
                wrapMode: TextArea.Wrap; selectByMouse: true; readOnly: true
                text: root.exportedVcard
                font.family: "monospace"; font.pixelSize: 12; color: Theme.palette.text
                background: Rectangle { radius: Theme.spacing.radiusSmall; color: Theme.palette.background; border.width: 1; border.color: Theme.palette.borderHairline }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: Theme.spacing.small
                LogosButton {
                    text: "Copy"
                    onClicked: { clipHelper.text = root.exportedVcard; clipHelper.selectAll(); clipHelper.copy() }
                }
                Item { Layout.fillWidth: true }
                LogosButton { text: "Close"; onClicked: exportPopup.close() }
            }
        }
    }
}
