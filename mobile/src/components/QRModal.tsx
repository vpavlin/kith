// Show a value (a kith://join invite) as a QR code, with a Copy button.
import React from "react";
import { Modal, View, Text, Pressable, StyleSheet, useWindowDimensions } from "react-native";
import QRCode from "react-native-qrcode-svg";
import * as Clipboard from "expo-clipboard";

const C = { bg: "#1e1e2e", surface: "#2a2a3c", text: "#cdd6f4", sub: "#9399b2", primary: "#89b4fa", border: "#313244", accent: "#a6e3a1" };

export function QRModal({ visible, value, title, onClose }: {
  visible: boolean; value: string; title: string; onClose: () => void;
}) {
  const [copied, setCopied] = React.useState(false);
  const { width } = useWindowDimensions();
  // Keep the QR + its white padding comfortably inside the sheet on narrow screens
  // (sheet has 20px padding, backdrop has 24px padding on each side).
  const qrSize = Math.max(160, Math.min(220, width - 24 * 2 - 20 * 2 - 28));
  const copy = async () => {
    await Clipboard.setStringAsync(value);
    setCopied(true);
    setTimeout(() => setCopied(false), 1500);
  };
  return (
    <Modal visible={visible} animationType="fade" transparent onRequestClose={onClose}>
      <View style={s.backdrop}>
        <View style={s.sheet}>
          <Text style={s.h}>{title}</Text>
          <Text style={s.sub}>Scan this on another device to join, or copy the link.</Text>
          <View style={s.qrWrap}>
            {!!value && <QRCode value={value} size={qrSize} backgroundColor="#ffffff" color="#000000" />}
          </View>
          <Text style={s.link} numberOfLines={2} selectable>{value}</Text>
          <Pressable style={[s.btn, { backgroundColor: copied ? C.accent : C.primary }]} onPress={copy}>
            <Text style={[s.btnT, { color: C.bg }]}>{copied ? "Copied!" : "Copy link"}</Text>
          </Pressable>
          <Pressable style={[s.btn, { backgroundColor: "transparent" }]} onPress={onClose} hitSlop={8}>
            <Text style={[s.btnT, { color: C.sub }]}>Close</Text>
          </Pressable>
        </View>
      </View>
    </Modal>
  );
}

const s = StyleSheet.create({
  backdrop: { flex: 1, backgroundColor: "rgba(0,0,0,0.6)", justifyContent: "center", padding: 24 },
  sheet: { backgroundColor: C.surface, borderRadius: 16, padding: 20, alignItems: "center", width: "100%", maxWidth: 400, alignSelf: "center", maxHeight: "88%" },
  h: { color: C.text, fontSize: 18, fontWeight: "700", marginBottom: 4 },
  sub: { color: C.sub, fontSize: 12, textAlign: "center", marginBottom: 14 },
  qrWrap: { backgroundColor: "#fff", padding: 14, borderRadius: 12 },
  link: { color: C.sub, fontSize: 11, marginTop: 12, textAlign: "center" },
  btn: { borderRadius: 10, paddingVertical: 12, alignItems: "center", alignSelf: "stretch", marginTop: 12 },
  btnT: { fontSize: 15, fontWeight: "700" },
});
