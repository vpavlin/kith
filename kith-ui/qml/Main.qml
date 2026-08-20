// Kith — pure-QML Basecamp view over the `kith` core module (kith ADR 0006/0007).
//
// Structural idiom copied from scala-ui/qml/CalendarView.qml: a synchronous
// logos.callModule bridge (core()/loamCore()), a j() double/triple-JSON-unwrap
// helper, a poll Timer (events are not reliably delivered to QML), and
// Logos.Theme/Logos.Controls styling (LogosText/LogosButton — the proven-safe
// baseline on the 0.2.0-era bundled design system — plus LogosTextField, which
// scala-ui already uses successfully in its identities popup).
//
// HARD RULE (explicit from the build brief): no per-item blocking IPC in QML.
// refresh() makes at most TWO calls regardless of how many books/contacts exist:
//   1) listBooks()            — one call, core already bakes in authorAddr +
//                                contactCount per book (kith_impl.cpp listBooks).
//   2) listContacts(bookId)   — one call for the selected book's whole contact
//                                list, already folded. Search/filter is client-side
//                                JS over that array — zero extra IPC per keystroke.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Logos.Theme
import Logos.Controls

Item {
    id: root
    width: 960
    height: 660

    // ── core bridge ──────────────────────────────────────────────────────────
    property bool ready: false
    function core(method, args) {
        if (typeof logos === "undefined" || !logos.callModule) return ""
        var r = logos.callModule("kith", method, args || [])
        return (r === undefined || r === null) ? "" : r
    }
    // Identity SERVICE lives in loam_core (loam ADR 0004 / kith ADR 0003 "Loam owns
    // WHO") — the view talks to it directly for the identity picker, exactly like
    // scala's loamCore() helper.
    function loamCore(method, args) {
        if (typeof logos === "undefined" || !logos.callModule) return ""
        try { var r = logos.callModule("loam_core", method, args || []); return (r === undefined || r === null) ? "" : r } catch (e) { return "" }
    }
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
    readonly property string minCore: "0.1.0"
    function verLt(a, b) {
        var pa = String(a).split("."), pb = String(b).split(".")
        for (var i = 0; i < 3; i++) { var x = parseInt(pa[i] || "0"), y = parseInt(pb[i] || "0"); if (x !== y) return x < y }
        return false
    }
    function checkCoreVersion() {
        root.coreVer = String(root.core("coreVersion", [])).replace(/^"|"$/g, "")
        root.coreOutOfDate = (root.coreVer === "" || root.verLt(root.coreVer, root.minCore))
    }

    // ── identities (loam_core service; UI is ours — same shape as scala) ───────
    property var identities: []            // [{id,kind,label,address,pubHex}]
    property string defaultIdentityId: "device"
    function refreshIdentities() {
        identities = root.j(loamCore("listIdentities", []), [])
        defaultIdentityId = root.j(loamCore("getDefaultIdentityId", []), "device")
    }
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
        root.ready = (typeof logos !== "undefined" && !!logos.callModule)
        if (root.ready) { root.checkCoreVersion(); root.refresh() }
    })

    Timer {
        interval: 3000; running: true; repeat: true
        onTriggered: root.refresh()
    }

    // The ONLY function that talks to the core on a data refresh — two calls, ever,
    // no matter how many books/contacts exist (see file header).
    function refresh() {
        if (!root.ready) return
        refreshIdentities()
        var bs = j(core("listBooks", []), [])
        // Multi-instance guard (scala's documented basecamp behaviour): don't let an
        // empty poll from a not-yet-loaded instance blank an already-populated list.
        if (bs.length === 0 && root.books.length > 0) { /* keep what we have */ }
        else root.books = bs
        if (root.selectedBookId !== "") {
            var stillThere = false
            for (var i = 0; i < root.books.length; i++) if (root.books[i].id === root.selectedBookId) stillThere = true
            if (!stillThere) { root.selectedBookId = ""; root.contacts = [] }
            else {
                var cs = j(core("listContacts", [root.selectedBookId]), [])
                if (cs.length === 0 && root.contacts.length > 0 && root.contactSearch === "") { /* keep */ }
                else root.contacts = cs
            }
        }
    }
    function selectBook(id) {
        root.selectedBookId = id
        root.contactSearch = ""
        if (typeof searchField !== "undefined") searchField.text = ""
        root.contacts = j(core("listContacts", [id]), [])
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
        color: "#f38ba8"
        ColumnLayout {
            id: bannerCol
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 16; rightMargin: 16 }
            spacing: 1
            LogosText { text: "⚠  Kith core is out of date" + (root.coreVer ? " (v" + root.coreVer + ")" : ""); color: "#11111b"; font.pixelSize: 14 }
            LogosText { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "Update the 'kith' package to " + root.minCore + "+ in Basecamp."; color: "#11111b"; font.pixelSize: 12 }
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

                LogosText { text: "Books"; color: Theme.palette.text; font.pixelSize: 18; font.weight: Theme.typography.weightMedium }

                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                    model: root.books
                    spacing: 2
                    delegate: Rectangle {
                        width: ListView.view.width; height: 44
                        radius: Theme.spacing.radiusSmall
                        color: root.selectedBookId === modelData.id ? Theme.palette.backgroundSecondary : "transparent"
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: Theme.spacing.small; anchors.rightMargin: Theme.spacing.small; spacing: Theme.spacing.small
                            ColumnLayout {
                                Layout.fillWidth: true; Layout.alignment: Qt.AlignVCenter; spacing: 1
                                LogosText { text: modelData.name || "(unnamed)"; color: Theme.palette.text; font.pixelSize: 14; Layout.fillWidth: true; elide: Text.ElideRight }
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
                                    onClicked: root.askConfirm(
                                        "Delete \"" + (modelData.name || "this book") + "\" and its " + modelData.contactCount + " contact(s)? This can't be undone.",
                                        function () { core("deleteBook", [modelData.id]); root.refresh() })
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

                LogosButton { Layout.fillWidth: true; text: "+ New book"; onClicked: newBookPopup.open() }
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
                    LogosButton { text: "Import vCard"; onClicked: importPopup.open() }
                    LogosButton {
                        text: "Export book"
                        onClicked: { root.exportedVcard = String(root.j(root.core("exportVcard", [root.selectedBookId, ""]), "")); exportPopup.open() }
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
                        color: (root.newBookIdentity === modelData.id || (root.newBookIdentity === "" && modelData.id === root.defaultIdentityId)) ? Theme.palette.primary : Theme.palette.backgroundSecondary
                        border.width: 1
                        border.color: (root.newBookIdentity === modelData.id || (root.newBookIdentity === "" && modelData.id === root.defaultIdentityId)) ? Theme.palette.primary : Theme.palette.borderHairline
                        implicitHeight: nbChipT.implicitHeight + 10; implicitWidth: nbChipT.implicitWidth + 22
                        LogosText { id: nbChipT; anchors.centerIn: parent; text: modelData.label; font.pixelSize: 12; color: Theme.palette.text }
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
                        var id = String(root.j(root.core("createBook", [newBookName.text.trim(), root.newBookIdentity || root.defaultIdentityId]), ""))
                        newBookPopup.close(); root.refresh()
                        if (id !== "") root.selectBook(id)
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
        if (root.editingContact) core("editContact", [root.selectedBookId, JSON.stringify(p)])
        else core("addContact", [root.selectedBookId, JSON.stringify(p)])
        contactPopup.close(); root.refresh()
    }
    function deleteContact() {
        if (root.editingContact) {
            var id = root.editingContact.id
            contactPopup.close()
            root.askConfirm("Delete " + root.contactName(root.editingContact) + "?", function () {
                core("deleteContact", [root.selectedBookId, id]); root.refresh()
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
                    onClicked: {
                        root.exportedVcard = String(root.j(root.core("exportVcard", [root.selectedBookId, root.editingContact.id]), ""))
                        exportPopup.open()
                    }
                }
                Item { Layout.fillWidth: true }
                LogosButton { text: "Cancel"; onClicked: contactPopup.close() }
                LogosButton { text: root.editingContact ? "Save" : "Add"; enabled: cName.text.trim().length > 0; onClicked: root.saveContact() }
            }
        }
    }

    // ── import vCard popup ──────────────────────────────────────────────────
    property string importResult: ""
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
                    text: "Import"; enabled: vcardInput.text.trim().length > 0
                    onClicked: {
                        var res = root.j(root.core("importVcard", [root.selectedBookId, vcardInput.text]), { imported: 0 })
                        root.importResult = "Imported " + (res.imported || 0) + " contact(s)."
                        root.refresh()
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
