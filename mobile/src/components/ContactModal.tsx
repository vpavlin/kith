// Add / edit a contact — kith ADR 0002 fields: name, multi-valued phones/emails/
// handles/addresses, notes. loamIdentity is shown read-only (set via add-author,
// not hand-typed here in v1).
import React, { useEffect, useState } from "react";
import { Modal, View, Text, TextInput, Pressable, ScrollView, StyleSheet, KeyboardAvoidingView, Platform } from "react-native";
import { SafeAreaView } from "react-native-safe-area-context";
import { Contact } from "../lib/store";

const C = {
  bg: "#1B1509", surface: "#241C11", text: "#EBDEC2", sub: "#B7A582",
  primary: "#CB9E60", border: "#33291A", accent: "#95B56C", danger: "#CE5C4C",
};

export type ContactDraft = Omit<Contact, "id" | "createdAt" | "updatedAt" | "authorAddr">;

function emptyDraft(): ContactDraft {
  return { name: { display: "" }, phones: [], emails: [], handles: [], addresses: [], notes: "" };
}

export function ContactModal({
  visible, initial, onSave, onDelete, onClose,
}: {
  visible: boolean;
  initial?: Contact;
  onSave: (d: ContactDraft) => void;
  onDelete?: () => void;
  onClose: () => void;
}) {
  const [draft, setDraft] = useState<ContactDraft>(emptyDraft());

  useEffect(() => {
    if (visible) setDraft(initial ? { ...initial } : emptyDraft());
  }, [visible, initial]);

  const setName = (k: string, v: string) => setDraft((d) => ({ ...d, name: { ...d.name, [k]: v } }));
  const addRow = (field: "phones" | "emails" | "handles") =>
    setDraft((d: any) => ({ ...d, [field]: [...d[field], field === "handles" ? { kind: "other", value: "" } : { label: "other", value: "" }] }));
  const setRow = (field: "phones" | "emails" | "handles", i: number, k: string, v: string) =>
    setDraft((d: any) => { const arr = [...d[field]]; arr[i] = { ...arr[i], [k]: v }; return { ...d, [field]: arr }; });
  const delRow = (field: "phones" | "emails" | "handles", i: number) =>
    setDraft((d: any) => ({ ...d, [field]: d[field].filter((_: any, j: number) => j !== i) }));

  const save = () => {
    if (!draft.name.display?.trim()) return;
    onSave({ ...draft, name: { ...draft.name, display: draft.name.display.trim() } });
  };

  return (
    <Modal visible={visible} animationType="slide" onRequestClose={onClose}>
      <SafeAreaView style={s.root} edges={["top", "left", "right", "bottom"]}>
      <KeyboardAvoidingView style={{ flex: 1 }} behavior={Platform.OS === "ios" ? "padding" : undefined}>
        <View style={s.header}>
          <Pressable onPress={onClose} hitSlop={10}><Text style={s.headerBtn}>Cancel</Text></Pressable>
          <Text style={s.headerTitle} numberOfLines={1}>{initial ? "Edit contact" : "New contact"}</Text>
          <Pressable onPress={save} hitSlop={10}><Text style={[s.headerBtn, { color: C.accent, fontWeight: "700" }]}>Save</Text></Pressable>
        </View>
        <ScrollView contentContainerStyle={s.body} keyboardShouldPersistTaps="handled">
          <Text style={s.label}>Name</Text>
          <TextInput style={s.input} placeholder="Display name" placeholderTextColor={C.sub} value={draft.name.display} onChangeText={(v) => setName("display", v)} />
          <View style={s.row2}>
            <TextInput style={[s.input, s.half]} placeholder="Given" placeholderTextColor={C.sub} value={draft.name.given || ""} onChangeText={(v) => setName("given", v)} />
            <TextInput style={[s.input, s.half]} placeholder="Family" placeholderTextColor={C.sub} value={draft.name.family || ""} onChangeText={(v) => setName("family", v)} />
          </View>
          <TextInput style={s.input} placeholder="Organization" placeholderTextColor={C.sub} value={draft.name.org || ""} onChangeText={(v) => setName("org", v)} />

          <Text style={s.label}>Phones</Text>
          {draft.phones.map((p, i) => (
            <View key={i} style={s.row2}>
              <TextInput style={[s.input, s.third]} placeholder="label" placeholderTextColor={C.sub} value={p.label} onChangeText={(v) => setRow("phones", i, "label", v)} />
              <TextInput style={[s.input, s.grow]} placeholder="number" placeholderTextColor={C.sub} keyboardType="phone-pad" value={p.value} onChangeText={(v) => setRow("phones", i, "value", v)} />
              <Pressable onPress={() => delRow("phones", i)} hitSlop={10}><Text style={s.del}>✕</Text></Pressable>
            </View>
          ))}
          <Pressable onPress={() => addRow("phones")}><Text style={s.add}>+ add phone</Text></Pressable>

          <Text style={s.label}>Emails</Text>
          {draft.emails.map((e, i) => (
            <View key={i} style={s.row2}>
              <TextInput style={[s.input, s.third]} placeholder="label" placeholderTextColor={C.sub} value={e.label} onChangeText={(v) => setRow("emails", i, "label", v)} />
              <TextInput style={[s.input, s.grow]} placeholder="email" placeholderTextColor={C.sub} keyboardType="email-address" autoCapitalize="none" value={e.value} onChangeText={(v) => setRow("emails", i, "value", v)} />
              <Pressable onPress={() => delRow("emails", i)} hitSlop={10}><Text style={s.del}>✕</Text></Pressable>
            </View>
          ))}
          <Pressable onPress={() => addRow("emails")}><Text style={s.add}>+ add email</Text></Pressable>

          <Text style={s.label}>Handles</Text>
          {draft.handles.map((h, i) => (
            <View key={i} style={s.row2}>
              <TextInput style={[s.input, s.third]} placeholder="kind" placeholderTextColor={C.sub} value={h.kind} onChangeText={(v) => setRow("handles", i, "kind", v)} />
              <TextInput style={[s.input, s.grow]} placeholder="@handle" placeholderTextColor={C.sub} autoCapitalize="none" value={h.value} onChangeText={(v) => setRow("handles", i, "value", v)} />
              <Pressable onPress={() => delRow("handles", i)} hitSlop={10}><Text style={s.del}>✕</Text></Pressable>
            </View>
          ))}
          <Pressable onPress={() => addRow("handles")}><Text style={s.add}>+ add handle</Text></Pressable>

          <Text style={s.label}>Notes</Text>
          <TextInput style={[s.input, s.notes]} multiline placeholder="Notes" placeholderTextColor={C.sub} value={draft.notes} onChangeText={(v) => setDraft((d) => ({ ...d, notes: v }))} />

          {draft.loamIdentity && (
            <>
              <Text style={s.label}>Loam identity</Text>
              <Text style={s.mono} selectable>{draft.loamIdentity.address}</Text>
              <Text style={s.sub}>{draft.loamIdentity.verified ? "verified" : "unverified"} · via {draft.loamIdentity.addedVia}</Text>
            </>
          )}

          {onDelete && (
            <Pressable style={s.deleteBtn} onPress={onDelete}>
              <Text style={s.deleteBtnT}>Delete contact</Text>
            </Pressable>
          )}
        </ScrollView>
      </KeyboardAvoidingView>
      </SafeAreaView>
    </Modal>
  );
}

const s = StyleSheet.create({
  root: { flex: 1, backgroundColor: C.bg },
  header: { flexDirection: "row", justifyContent: "space-between", alignItems: "center", padding: 16, borderBottomWidth: 1, borderBottomColor: C.border },
  headerBtn: { color: C.primary, fontSize: 15 },
  headerTitle: { color: C.text, fontSize: 16, fontWeight: "700" },
  body: { padding: 16, paddingBottom: 60 },
  label: { color: C.sub, fontSize: 12, fontWeight: "700", textTransform: "uppercase", marginTop: 18, marginBottom: 6 },
  input: { backgroundColor: C.surface, color: C.text, borderRadius: 10, padding: 12, marginBottom: 8, borderWidth: 1, borderColor: C.border },
  row2: { flexDirection: "row", gap: 8, alignItems: "center" },
  half: { flex: 1 },
  third: { flex: 0.9 },
  grow: { flex: 2 },
  notes: { minHeight: 80, textAlignVertical: "top" },
  add: { color: C.primary, fontSize: 13, marginTop: 4, marginBottom: 8 },
  del: { color: C.danger, fontSize: 18, paddingHorizontal: 6 },
  mono: { color: C.text, fontSize: 12, fontFamily: Platform.select({ ios: "Menlo", android: "monospace" }) },
  sub: { color: C.sub, fontSize: 11, marginTop: 2 },
  deleteBtn: { marginTop: 30, borderRadius: 10, paddingVertical: 12, alignItems: "center", backgroundColor: "rgba(243,139,168,0.15)" },
  deleteBtnT: { color: C.danger, fontWeight: "700" },
});
