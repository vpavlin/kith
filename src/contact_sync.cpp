#include "contact_sync.h"
#include "logos_transport.hpp"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <cstdio>
#include <cstring>

using Tx = logos_transport::Transport<LogosMap>;

ContactSync::ContactSync() {}
ContactSync::~ContactSync() {}

void ContactSync::setEventHandler(OnEventReceived h) { m_onEvent = std::move(h); }
void ContactSync::setStatusHandler(OnSyncStatus h) { m_onStatus = std::move(h); }

std::string ContactSync::topicForBook(const std::string &bookId) {
    return "/kith/1/" + bookId + "/json";
}

bool ContactSync::isSyncing(const std::string &bookId) const {
    return m_activeTopics.count(bookId) > 0;
}

void ContactSync::startSync(const std::string &bookId, const std::string &encryptionKey) {
    if (m_activeTopics.count(bookId)) return;   // already syncing — idempotent
    m_activeTopics[bookId] = encryptionKey;
    if (m_tx && m_tx->ready()) {
        std::string topic = topicForBook(bookId);
        m_tx->join(topic);
        fprintf(stderr, "ContactSync: joined topic %s\n", topic.c_str());
    }
    if (m_onStatus) m_onStatus(bookId, "syncing");
}

void ContactSync::stopSync(const std::string &bookId) {
    if (!m_activeTopics.count(bookId)) return;
    m_activeTopics.erase(bookId);
    if (m_onStatus) m_onStatus(bookId, "stopped");
}

void ContactSync::sendEvent(const std::string &bookId, const std::string &eventJson) {
    if (!m_activeTopics.count(bookId) || !m_tx || !m_tx->ready()) return;
    // Derive the AEAD nonce from the event's stable id (logos-sync ADR 0011): a
    // re-sent immutable event then seals byte-identically, so the fleet store dedups
    // it instead of accumulating a fresh random-nonce copy every time. Every event
    // carries a UUID `id` (mkEvent → generateUuid), including per-call SYNC_REQ
    // frames, so control frames each get a distinct nonce and are never collapsed.
    // Fallback to a random token if a frame ever arrives without an id.
    std::string sealId;
    auto j = nlohmann::json::parse(eventJson, nullptr, false);
    if (j.is_object()) sealId = j.value("id", std::string());
    if (sealId.empty()) {
        unsigned char rnd[16];
        RAND_bytes(rnd, 16);
        static const char *hexd = "0123456789abcdef";
        sealId.reserve(32);
        for (int i = 0; i < 16; i++) { sealId += hexd[rnd[i] >> 4]; sealId += hexd[rnd[i] & 0xf]; }
    }
    std::string sealed = seal(bookId, eventJson, sealId);
    m_tx->send(topicForBook(bookId), sealed);
}

// Deterministic 12-byte AEAD nonce derived from a seal id (logos-sync ADR 0011).
// Byte-identical to the phone's crypto.ts nonceFor() and to kym/qaku's scheme
// (cipher-agnostic — kith keeps AES-256-GCM). The HMAC key is the same 32-byte AES
// key the cipher uses; domain string is "kith/nonce/v1|".
static std::vector<unsigned char> nonceFor(const std::vector<unsigned char> &key,
                                           const std::string &sealId) {
    std::string msg = "kith/nonce/v1|" + sealId;
    unsigned char mac[32];
    unsigned int maclen = 0;
    HMAC(EVP_sha256(), key.data(), (int)key.size(),
         (const unsigned char *)msg.data(), msg.size(), mac, &maclen);
    return std::vector<unsigned char>(mac, mac + 12);
}

// ── Crypto (byte-identical to scala's CalendarSync::seal/open) ────────────────
std::string ContactSync::seal(const std::string &bookId, const std::string &plaintext,
                              const std::string &sealId) {
    auto it = m_activeTopics.find(bookId);
    if (it == m_activeTopics.end()) return plaintext; // fallback

    const std::string &keyHex = it->second;
    std::vector<unsigned char> key(32);
    for (size_t i = 0; i < 32 && i * 2 < keyHex.size(); i++)
        sscanf(keyHex.c_str() + i * 2, "%2hhx", &key[i]);

    // ADR 0011: nonce = HMAC-SHA256(key, "kith/nonce/v1|"+sealId)[0..11], NOT random.
    std::vector<unsigned char> nonce = nonceFor(key, sealId);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    std::vector<unsigned char> ciphertext(plaintext.size() + 32);
    int len = 0, ciphertextLen = 0;

    EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key.data(), nonce.data());
    EVP_EncryptUpdate(ctx, ciphertext.data(), &len, (const unsigned char *)plaintext.data(), (int)plaintext.size());
    ciphertextLen = len;
    EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len);
    ciphertextLen += len;

    unsigned char tag[16];
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag);
    EVP_CIPHER_CTX_free(ctx);

    std::string result;
    result.resize(12 + 16 + ciphertextLen);
    memcpy(&result[0], nonce.data(), 12);
    memcpy(&result[12], tag, 16);
    memcpy(&result[28], ciphertext.data(), ciphertextLen);
    return result;
}

std::optional<std::string> ContactSync::open(const std::string &bookId, const std::string &sealedBytes) {
    auto it = m_activeTopics.find(bookId);
    if (it == m_activeTopics.end()) return std::nullopt;
    if (sealedBytes.size() < 28 + 16) return std::nullopt;

    const std::string &keyHex = it->second;
    std::vector<unsigned char> key(32);
    for (size_t i = 0; i < 32 && i * 2 < keyHex.size(); i++)
        sscanf(keyHex.c_str() + i * 2, "%2hhx", &key[i]);

    std::vector<unsigned char> nonce(12);
    unsigned char tag[16];
    memcpy(nonce.data(), sealedBytes.data(), 12);
    memcpy(tag, sealedBytes.data() + 12, 16);
    int ctLen = (int)(sealedBytes.size() - 28);
    const unsigned char *ct = (const unsigned char *)sealedBytes.data() + 28;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    std::string plaintext(ctLen, '\0');
    int outLen = 0;

    EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key.data(), nonce.data());
    EVP_DecryptUpdate(ctx, (unsigned char *)plaintext.data(), &outLen, ct, ctLen);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, tag);

    int finalLen = 0;
    int ok = EVP_DecryptFinal_ex(ctx, (unsigned char *)plaintext.data() + outLen, &finalLen);
    EVP_CIPHER_CTX_free(ctx);

    if (ok <= 0) return std::nullopt;   // auth failure
    plaintext.resize(outLen + finalLen);
    return plaintext;
}

// ── Transport integration ──────────────────────────────────────────────────────
void ContactSync::handleReceive(const std::string &topic, const std::string &sealedOnceDecoded) {
    for (auto &[bookId, _] : m_activeTopics) {
        if (topicForBook(bookId) != topic) continue;
        // The transport peeled ONE base64 layer ("app peels the rest"). Try to open
        // as-is first (desktop→desktop), then peel one more base64 layer and retry
        // (a mobile peer double-base64s) — same two-attempt recipe as
        // CalendarSync::handleReceive.
        auto plain = open(bookId, sealedOnceDecoded);
        if (!plain) {
            std::string peeledAgain = logos_transport::b64detail::decode(sealedOnceDecoded);
            if (!peeledAgain.empty()) plain = open(bookId, peeledAgain);
        }
        if (!plain) {
            fprintf(stderr, "ContactSync: failed to open message for %s (tried single+double base64 depth)\n", bookId.c_str());
            return;
        }
        if (m_onEvent) m_onEvent(bookId, *plain);
        return;
    }
    fprintf(stderr, "ContactSync: received message for unknown topic %s\n", topic.c_str());
}

void ContactSync::setTransport(std::optional<Tx> tx) { m_tx = std::move(tx); }

void ContactSync::bootstrap() {
    if (m_tx && m_tx->ready()) return;
    if (m_tx) m_tx->bootstrap();
}

bool ContactSync::ready() const {
    return m_tx.has_value() && m_tx->ready();
}
