// Kith mobile — address-book peer over Logos Delivery (SDS channels), on the same
// wire as the desktop core (kith_engine.hpp / contact_sync.cpp). Books list + a
// per-book contacts screen with search/add/edit/delete, vCard import/export, and
// join-a-book via kith://join link (paste or QR scan).
import React, { useEffect, useState, useCallback, useMemo } from "react";
import {
  View, Text, TextInput, Pressable, ScrollView, StyleSheet, Alert, Modal, KeyboardAvoidingView, Platform, FlatList,
} from "react-native";
import { StatusBar } from "expo-status-bar";
import { SafeAreaProvider, SafeAreaView } from "react-native-safe-area-context";
import * as Clipboard from "expo-clipboard";

import { store, Book, Contact } from "./src/lib/store";
import {
  onChange, startSyncing, joinFromInvite, createBook, deleteBook,
  addContact, editContact, deleteContact, searchContacts, buildInvite,
  getSharedNode, setSharedNode, getDeviceId,
} from "./src/lib/contacts";
import { deliveryAvailable, getDebug, refreshDebug } from "./src/lib/kith-sync";
import { SharedNodeStatus } from "./src/lib/loam-transport-pkg/src/SharedNodeStatus";
import { ContactModal, ContactDraft } from "./src/components/ContactModal";
import { QRModal } from "./src/components/QRModal";
import { ScanModal } from "./src/components/ScanModal";
import { importVcard, exportOneVcard, exportManyVcard } from "./src/lib/vcard";
import { shortAddr } from "./src/lib/identity";

const C = {
  bg: "#1e1e2e", surface: "#2a2a3c", text: "#cdd6f4", sub: "#9399b2",
  primary: "#89b4fa", border: "#313244", accent: "#a6e3a1", danger: "#f38ba8",
};

function msg(e: unknown): string { return e instanceof Error ? e.message : String(e); }

export default function App() {
  const [books, setBooks] = useState<Book[]>([]);
  const [activeBookId, setActiveBookId] = useState<string | null>(null);
  const [contacts, setContacts] = useState<Contact[]>([]);
  const [status, setStatus] = useState("starting");
  const [shared, setShared] = useState(false);
  const [deviceId, setDeviceId] = useState("");

  const [query, setQuery] = useState("");
  const [searchResults, setSearchResults] = useState<{ bookId: string; contact: Contact }[]>([]);

  const [newBookName, setNewBookName] = useState("");
  const [showNewBook, setShowNewBook] = useState(false);
  const [joinLink, setJoinLink] = useState("");
  const [showJoin, setShowJoin] = useState(false);
  const [scanning, setScanning] = useState(false);

  const [contactModal, setContactModal] = useState<{ open: boolean; editing?: Contact }>({ open: false });
  const [shareBook, setShareBook] = useState<Book | null>(null);
  const [vcardText, setVcardText] = useState<string | null>(null); // shown for export copy
  const [importText, setImportText] = useState("");
  const [showImport, setShowImport] = useState(false);

  const refresh = useCallback(async () => {
    setBooks(await store.listBooks());
    if (activeBookId) setContacts(await store.contactsFor(activeBookId));
  }, [activeBookId]);

  useEffect(() => { const off = onChange(() => { void refresh(); }); return off; }, [refresh]);
  useEffect(() => { void refresh(); }, [activeBookId]);

  useEffect(() => {
    void (async () => {
      setDeviceId(await getDeviceId());
      setShared(await getSharedNode());
      try {
        await startSyncing(undefined, (s) => setStatus(s));
      } catch (e) {
        setStatus("error: " + msg(e));
      }
      await refresh();
    })();
  }, []);

  useEffect(() => {
    if (!query.trim()) { setSearchResults([]); return; }
    void searchContacts(query).then(setSearchResults);
  }, [query]);

  const activeBook = books.find((b) => b.id === activeBookId) || null;

  const onCreateBook = async () => {
    if (!newBookName.trim()) return;
    const b = await createBook(newBookName);
    setNewBookName("");
    setShowNewBook(false);
    setActiveBookId(b.id);
  };

  const onJoin = async (link: string) => {
    try {
      const b = await joinFromInvite(link.trim());
      if (!b) { Alert.alert("Invalid link", "That doesn't look like a kith:// invite."); return; }
      setJoinLink("");
      setShowJoin(false);
      setScanning(false);
      setActiveBookId(b.id);
    } catch (e) {
      Alert.alert("Join failed", msg(e));
    }
  };

  const onSaveContact = async (draft: ContactDraft) => {
    if (!activeBookId) return;
    try {
      if (contactModal.editing) {
        await editContact(activeBookId, { ...contactModal.editing, ...draft });
      } else {
        await addContact(activeBookId, draft);
      }
      setContactModal({ open: false });
    } catch (e) {
      Alert.alert("Save failed", msg(e));
    }
  };
  const onDeleteContact = async () => {
    if (!activeBookId || !contactModal.editing) return;
    await deleteContact(activeBookId, contactModal.editing.id);
    setContactModal({ open: false });
  };

  const onDeleteBook = (b: Book) => {
    Alert.alert("Remove book", `Remove "${b.name}" from this device? Other devices keep their copy.`, [
      { text: "Cancel", style: "cancel" },
      { text: "Remove", style: "destructive", onPress: () => { void deleteBook(b.id); if (activeBookId === b.id) setActiveBookId(null); } },
    ]);
  };

  const onImportVcard = async () => {
    if (!activeBookId || !importText.trim()) return;
    const parsed = importVcard(importText);
    for (const c of parsed) await addContact(activeBookId, c);
    setImportText("");
    setShowImport(false);
    Alert.alert("Imported", `${parsed.length} contact(s) imported.`);
  };
  const onExportBook = () => {
    if (!activeBook) return;
    setVcardText(exportManyVcard(contacts));
  };
  const onExportOne = (c: Contact) => setVcardText(exportOneVcard(c));

  const copyVcard = async () => {
    if (vcardText) await Clipboard.setStringAsync(vcardText);
  };

  return (
    <SafeAreaProvider>
      <SafeAreaView style={s.root} edges={["top", "left", "right"]}>
        <StatusBar style="light" />
        <SharedNodeStatus appName="Kith" showSync />

        {/* ── header ─────────────────────────────────────────────────────── */}
        <View style={s.header}>
          {activeBook ? (
            <Pressable onPress={() => setActiveBookId(null)}><Text style={s.back}>‹ Books</Text></Pressable>
          ) : (
            <Text style={s.title}>Kith</Text>
          )}
          <Text style={s.statusChip}>{status}</Text>
        </View>

        {!activeBook && (
          <>
            <View style={s.searchRow}>
              <TextInput
                style={s.search}
                placeholder="Search all contacts…"
                placeholderTextColor={C.sub}
                value={query}
                onChangeText={setQuery}
              />
            </View>

            {query.trim() ? (
              <FlatList
                data={searchResults}
                keyExtractor={(item) => item.contact.id}
                renderItem={({ item }) => (
                  <Pressable
                    style={s.contactRow}
                    onPress={() => { setActiveBookId(item.bookId); setQuery(""); setTimeout(() => setContactModal({ open: true, editing: item.contact }), 0); }}
                  >
                    <Text style={s.contactName}>{item.contact.name?.display || "(no name)"}</Text>
                    <Text style={s.contactSub}>
                      {books.find((b) => b.id === item.bookId)?.name || item.bookId}
                    </Text>
                  </Pressable>
                )}
                ListEmptyComponent={<Text style={s.empty}>No matches.</Text>}
              />
            ) : (
              <>
                <FlatList
                  data={books}
                  keyExtractor={(b) => b.id}
                  renderItem={({ item }) => (
                    <Pressable style={s.bookRow} onPress={() => setActiveBookId(item.id)}>
                      <View style={{ flex: 1 }}>
                        <Text style={s.bookName}>{item.name}</Text>
                        <Text style={s.contactSub}>{item.contactCount ?? 0} contact{item.contactCount === 1 ? "" : "s"}</Text>
                      </View>
                      <Pressable onPress={() => setShareBook(item)} style={s.iconBtn}><Text style={s.iconBtnT}>Share</Text></Pressable>
                      <Pressable onPress={() => onDeleteBook(item)} style={s.iconBtn}><Text style={[s.iconBtnT, { color: C.danger }]}>Remove</Text></Pressable>
                    </Pressable>
                  )}
                  ListEmptyComponent={<Text style={s.empty}>No books yet. Create one, or join with an invite link.</Text>}
                />
                <View style={s.footerRow}>
                  <Pressable style={s.footerBtn} onPress={() => setShowNewBook(true)}><Text style={s.footerBtnT}>+ New book</Text></Pressable>
                  <Pressable style={s.footerBtn} onPress={() => setShowJoin(true)}><Text style={s.footerBtnT}>Join book</Text></Pressable>
                </View>
                <View style={s.footerRow}>
                  <Pressable
                    style={[s.footerBtn, { backgroundColor: "transparent", borderWidth: 1, borderColor: C.border }]}
                    onPress={async () => { const on = !shared; setShared(on); await setSharedNode(on); Alert.alert("Restart required", "Toggling the shared node takes effect after restarting the app."); }}
                  >
                    <Text style={[s.footerBtnT, { color: C.text }]}>Shared node: {shared ? "on" : "off"}</Text>
                  </Pressable>
                </View>
                <Text style={s.deviceId} selectable>device: {shortAddr(deviceId)}</Text>
              </>
            )}
          </>
        )}

        {activeBook && (
          <>
            <Text style={s.bookTitleBig}>{activeBook.name}</Text>
            <View style={s.searchRow}>
              <TextInput style={s.search} placeholder="Search this book…" placeholderTextColor={C.sub} value={query} onChangeText={setQuery} />
            </View>
            <FlatList
              data={query.trim() ? contacts.filter((c) => (c.name?.display || "").toLowerCase().includes(query.toLowerCase())) : contacts}
              keyExtractor={(c) => c.id}
              renderItem={({ item }) => (
                <Pressable style={s.contactRow} onPress={() => setContactModal({ open: true, editing: item })} onLongPress={() => onExportOne(item)}>
                  <Text style={s.contactName}>{item.name?.display || "(no name)"}</Text>
                  {!!item.phones?.length && <Text style={s.contactSub}>{item.phones[0].value}</Text>}
                  {item.loamIdentity && <Text style={s.contactBadge}>loam</Text>}
                </Pressable>
              )}
              ListEmptyComponent={<Text style={s.empty}>No contacts yet.</Text>}
            />
            <View style={s.footerRow}>
              <Pressable style={s.footerBtn} onPress={() => setContactModal({ open: true })}><Text style={s.footerBtnT}>+ Add contact</Text></Pressable>
              <Pressable style={s.footerBtn} onPress={() => setShowImport(true)}><Text style={s.footerBtnT}>Import vCard</Text></Pressable>
              <Pressable style={s.footerBtn} onPress={onExportBook}><Text style={s.footerBtnT}>Export all</Text></Pressable>
            </View>
          </>
        )}

        {/* ── modals ─────────────────────────────────────────────────────── */}
        <ContactModal
          visible={contactModal.open}
          initial={contactModal.editing}
          onSave={onSaveContact}
          onDelete={contactModal.editing ? onDeleteContact : undefined}
          onClose={() => setContactModal({ open: false })}
        />

        <QRModal visible={!!shareBook} value={shareBook ? buildInvite(shareBook) : ""} title={`Share "${shareBook?.name ?? ""}"`} onClose={() => setShareBook(null)} />

        <Modal visible={showNewBook} animationType="slide" transparent onRequestClose={() => setShowNewBook(false)}>
          <View style={s.backdrop}>
            <View style={s.sheet}>
              <Text style={s.sheetTitle}>New book</Text>
              <TextInput style={s.input} placeholder="Book name" placeholderTextColor={C.sub} value={newBookName} onChangeText={setNewBookName} autoFocus />
              <Pressable style={s.primaryBtn} onPress={onCreateBook}><Text style={s.primaryBtnT}>Create</Text></Pressable>
              <Pressable style={s.textBtn} onPress={() => setShowNewBook(false)}><Text style={s.textBtnT}>Cancel</Text></Pressable>
            </View>
          </View>
        </Modal>

        <Modal visible={showJoin} animationType="slide" transparent onRequestClose={() => setShowJoin(false)}>
          <KeyboardAvoidingView style={s.backdrop} behavior={Platform.OS === "ios" ? "padding" : undefined}>
            <View style={s.sheet}>
              <Text style={s.sheetTitle}>Join a book</Text>
              <TextInput style={s.input} placeholder="kith://join?id=…&key=…" placeholderTextColor={C.sub} value={joinLink} onChangeText={setJoinLink} autoCapitalize="none" />
              <Pressable style={s.primaryBtn} onPress={() => onJoin(joinLink)}><Text style={s.primaryBtnT}>Join</Text></Pressable>
              <Pressable style={s.textBtn} onPress={() => { setShowJoin(false); setScanning(true); }}><Text style={s.textBtnT}>Scan QR instead</Text></Pressable>
              <Pressable style={s.textBtn} onPress={() => setShowJoin(false)}><Text style={s.textBtnT}>Cancel</Text></Pressable>
            </View>
          </KeyboardAvoidingView>
        </Modal>

        <ScanModal visible={scanning} onScanned={(data) => onJoin(data)} onClose={() => setScanning(false)} />

        <Modal visible={showImport} animationType="slide" transparent onRequestClose={() => setShowImport(false)}>
          <KeyboardAvoidingView style={s.backdrop} behavior={Platform.OS === "ios" ? "padding" : undefined}>
            <View style={s.sheet}>
              <Text style={s.sheetTitle}>Import vCard</Text>
              <Text style={s.sheetSub}>Paste one or more BEGIN:VCARD…END:VCARD blocks.</Text>
              <TextInput style={[s.input, { minHeight: 140, textAlignVertical: "top" }]} multiline value={importText} onChangeText={setImportText} placeholder="BEGIN:VCARD..." placeholderTextColor={C.sub} />
              <Pressable style={s.primaryBtn} onPress={onImportVcard}><Text style={s.primaryBtnT}>Import</Text></Pressable>
              <Pressable style={s.textBtn} onPress={() => setShowImport(false)}><Text style={s.textBtnT}>Cancel</Text></Pressable>
            </View>
          </KeyboardAvoidingView>
        </Modal>

        <Modal visible={!!vcardText} animationType="slide" transparent onRequestClose={() => setVcardText(null)}>
          <View style={s.backdrop}>
            <View style={s.sheet}>
              <Text style={s.sheetTitle}>vCard</Text>
              <ScrollView style={{ maxHeight: 260 }}>
                <Text style={s.mono} selectable>{vcardText}</Text>
              </ScrollView>
              <Pressable style={s.primaryBtn} onPress={copyVcard}><Text style={s.primaryBtnT}>Copy to clipboard</Text></Pressable>
              <Pressable style={s.textBtn} onPress={() => setVcardText(null)}><Text style={s.textBtnT}>Close</Text></Pressable>
            </View>
          </View>
        </Modal>
      </SafeAreaView>
    </SafeAreaProvider>
  );
}

const s = StyleSheet.create({
  root: { flex: 1, backgroundColor: C.bg },
  header: { flexDirection: "row", justifyContent: "space-between", alignItems: "center", paddingHorizontal: 16, paddingVertical: 10 },
  title: { color: C.text, fontSize: 22, fontWeight: "800" },
  back: { color: C.primary, fontSize: 16 },
  statusChip: { color: C.sub, fontSize: 11, textTransform: "uppercase" },
  bookTitleBig: { color: C.text, fontSize: 20, fontWeight: "800", paddingHorizontal: 16, marginBottom: 4 },
  searchRow: { paddingHorizontal: 16, marginBottom: 8 },
  search: { backgroundColor: C.surface, color: C.text, borderRadius: 10, padding: 10, borderWidth: 1, borderColor: C.border },
  bookRow: { flexDirection: "row", alignItems: "center", paddingHorizontal: 16, paddingVertical: 14, borderBottomWidth: 1, borderBottomColor: C.border },
  bookName: { color: C.text, fontSize: 16, fontWeight: "700" },
  contactRow: { paddingHorizontal: 16, paddingVertical: 12, borderBottomWidth: 1, borderBottomColor: C.border, flexDirection: "row", alignItems: "center", gap: 8 },
  contactName: { color: C.text, fontSize: 15, fontWeight: "600", flex: 1 },
  contactSub: { color: C.sub, fontSize: 12 },
  contactBadge: { color: C.accent, fontSize: 10, fontWeight: "700", borderWidth: 1, borderColor: C.accent, borderRadius: 6, paddingHorizontal: 5, paddingVertical: 1 },
  empty: { color: C.sub, textAlign: "center", marginTop: 40 },
  iconBtn: { paddingHorizontal: 8, paddingVertical: 6 },
  iconBtnT: { color: C.primary, fontSize: 12, fontWeight: "700" },
  footerRow: { flexDirection: "row", gap: 10, paddingHorizontal: 16, paddingVertical: 8 },
  footerBtn: { flex: 1, backgroundColor: C.primary, borderRadius: 10, paddingVertical: 12, alignItems: "center" },
  footerBtnT: { color: C.bg, fontWeight: "700", fontSize: 13 },
  deviceId: { color: C.sub, fontSize: 10, textAlign: "center", marginTop: 4, marginBottom: 10 },
  backdrop: { flex: 1, backgroundColor: "rgba(0,0,0,0.6)", justifyContent: "flex-end" },
  sheet: { backgroundColor: C.surface, borderTopLeftRadius: 16, borderTopRightRadius: 16, padding: 20 },
  sheetTitle: { color: C.text, fontSize: 18, fontWeight: "700", marginBottom: 4 },
  sheetSub: { color: C.sub, fontSize: 12, marginBottom: 10 },
  input: { backgroundColor: C.bg, color: C.text, borderRadius: 10, padding: 12, marginTop: 10, borderWidth: 1, borderColor: C.border },
  primaryBtn: { backgroundColor: C.primary, borderRadius: 10, paddingVertical: 12, alignItems: "center", marginTop: 14 },
  primaryBtnT: { color: C.bg, fontWeight: "700" },
  textBtn: { paddingVertical: 12, alignItems: "center" },
  textBtnT: { color: C.sub },
  mono: { color: C.text, fontSize: 11, fontFamily: Platform.select({ ios: "Menlo", android: "monospace" }) },
});
