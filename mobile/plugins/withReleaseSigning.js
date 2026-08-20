/**
 * withReleaseSigning — sign release builds with the real Kith key.
 *
 * Expo's template signs BOTH debug and release with the checked-in debug
 * keystore. That key is the well-known Android debug key: not secret, and
 * shared — anyone could sign an APK your phone would accept as an update to
 * Kith. It's also a one-way door: Android identifies an app by its signing
 * certificate, so switching keys after publishing forces every user to
 * uninstall/reinstall.
 *
 * Credentials are NOT in this repo (it's public, and losing the key means the
 * app can never be updated again). They live in the user-global
 * ~/.gradle/gradle.properties:
 *
 *   KITH_STORE_FILE=/home/vpavlin/keystores/kith-release.jks
 *   KITH_STORE_PASSWORD=…   KITH_KEY_ALIAS=kith   KITH_KEY_PASSWORD=…
 *
 * As of this build there is no kith-release.jks yet (see keystores/) — the
 * absent-properties fallback below means this simply builds with the debug key
 * until one is generated and the properties are added.
 *
 * If those properties are absent (e.g. a fresh clone / CI without secrets) the
 * build falls back to the debug key rather than failing.
 */
const { withAppBuildGradle } = require("@expo/config-plugins");

const RELEASE_SIGNING_CONFIG = `
        release {
            // Credentials come from ~/.gradle/gradle.properties (outside this repo).
            if (project.hasProperty('KITH_STORE_FILE')) {
                storeFile file(project.property('KITH_STORE_FILE'))
                storePassword project.property('KITH_STORE_PASSWORD')
                keyAlias project.property('KITH_KEY_ALIAS')
                keyPassword project.property('KITH_KEY_PASSWORD')
            }
            // F-Droid verifies APKs via the v1 (JAR) signature; RN 0.86's AGP defaults v1 OFF,
            // producing v2-only APKs F-Droid rejects with "failed to verify". Force v1 on.
            enableV1Signing true
            enableV2Signing true
        }`;

module.exports = (config) =>
  withAppBuildGradle(config, (cfg) => {
    let s = cfg.modResults.contents;

    if (!s.includes("KITH_STORE_FILE")) {
      // 1) add a `release` signing config alongside the template's `debug` one
      const before = s;
      s = s.replace(/(signingConfigs\s*\{)/, `$1${RELEASE_SIGNING_CONFIG}`);
      if (s === before) throw new Error("withReleaseSigning: signingConfigs block not found");

      // 2) point the release buildType at it, falling back to debug when unsigned.
      //    Anchored on the template's caution comment so we only hit the release
      //    buildType (the string `signingConfig signingConfigs.debug` appears twice).
      const anchor =
        /(\/\/ Caution! In production[^\n]*\n\s*\/\/ see [^\n]*\n\s*)signingConfig signingConfigs\.debug/;
      if (!anchor.test(s)) throw new Error("withReleaseSigning: release buildType anchor not found");
      s = s.replace(
        anchor,
        `$1signingConfig project.hasProperty('KITH_STORE_FILE') ? signingConfigs.release : signingConfigs.debug`
      );

      cfg.modResults.contents = s;
    }
    return cfg;
  });
