/**
 * withOptionalCamera — the camera is only for scanning kith://join QR invites, so it
 * must never make Kith "not compatible" with a device.
 *
 * Declaring the CAMERA permission makes Android (and F-Droid's compatibility check)
 * IMPLY a *required* android.hardware.camera feature, which hides the app on devices
 * without a (back) camera. Declare the features explicitly as required=false; the
 * scan button then just finds no camera at runtime.
 *
 * Also strips RECORD_AUDIO with tools:node="remove" — kith never records audio
 * (expo-camera is configured with recordAudioAndroid:false; this also covers a
 * non-clean prebuild or a library manifest re-adding it).
 *
 * Persistent across `expo prebuild` (applied to the generated AndroidManifest.xml).
 */
const { withAndroidManifest, AndroidConfig } = require("@expo/config-plugins");

const OPTIONAL_FEATURES = ["android.hardware.camera", "android.hardware.camera.autofocus"];
const REMOVED_PERMISSIONS = ["android.permission.RECORD_AUDIO"];

module.exports = function withOptionalCamera(config) {
  return withAndroidManifest(config, (cfg) => {
    const manifest = AndroidConfig.Manifest.ensureToolsAvailable(cfg.modResults).manifest;

    manifest["uses-feature"] = (manifest["uses-feature"] || []).filter(
      (f) => !OPTIONAL_FEATURES.includes(f.$["android:name"]),
    );
    for (const name of OPTIONAL_FEATURES) {
      manifest["uses-feature"].push({ $: { "android:name": name, "android:required": "false" } });
    }

    manifest["uses-permission"] = (manifest["uses-permission"] || []).filter(
      (p) => !REMOVED_PERMISSIONS.includes(p.$["android:name"]),
    );
    for (const name of REMOVED_PERMISSIONS) {
      manifest["uses-permission"].push({ $: { "android:name": name, "tools:node": "remove" } });
    }
    return cfg;
  });
};
