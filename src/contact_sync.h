#pragma once
// contact_sync.h — kith's transport-backed sync, modeled directly on scala's
// CalendarSync (src/calendar_sync.h) but trimmed to the CRDT-only path: kith never
// had scala's legacy SyncMessage/HMAC message shape, so there is no equivalent to
// carry over — every wire message here is a raw logos_sync::Event's JSON, sealed.
//
// One log == one book (kith ADR 0004/0006); one content topic == one book, exactly
// mirroring scala's one-topic-per-calendar. Topic: /kith/1/<bookId>/json.
//
// Crypto: byte-identical recipe to scala's CalendarSync (kith ADR "reuse scala's
// exact sealing"): AES-256-GCM, 32-byte key (hex), wire = nonce(12) || tag(16) ||
// ciphertext. The 32-byte key is the book's `key` field (kith_engine::BookReg /
// ContactStore::BookReg), the same 72-char dashed-UUID-pair scala/qaku/kym use,
// truncated/hex-parsed exactly like CalendarSync::seal/open already do (sscanf
// consumes at most 64 hex chars = 32 bytes; a 72-char key still yields a valid,
// stable 32-byte key — same behaviour scala ships with today).

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "logos_transport.hpp"

using LogosMap = nlohmann::json;

class ContactSync {
public:
    using OnEventReceived = std::function<void(const std::string &bookId, const std::string &eventJson)>;
    using OnSyncStatus = std::function<void(const std::string &bookId, const std::string &status)>;

    ContactSync();
    ~ContactSync();

    void setEventHandler(OnEventReceived h);
    void setStatusHandler(OnSyncStatus h);

    /// Start syncing a book (subscribe to its topic via transport, remember its key).
    void startSync(const std::string &bookId, const std::string &encryptionKey);
    /// Stop syncing a book (does not touch its stored log).
    void stopSync(const std::string &bookId);
    /// Whether a book is currently syncing.
    bool isSyncing(const std::string &bookId) const;

    /// CRDT: seal + broadcast one event's JSON on the book's channel. No-op if the
    /// book isn't syncing or the transport isn't ready yet (event still lands
    /// locally via the caller's own store append — this is fire-and-forget).
    void sendEvent(const std::string &bookId, const std::string &eventJson);

    /// Topic format: /kith/1/<bookId>/json
    static std::string topicForBook(const std::string &bookId);

    /// Set the constructed transport (called from KithImpl after Ops wiring).
    void setTransport(std::optional<logos_transport::Transport<LogosMap>> tx);
    /// Bring the node up (idempotent, poll-safe).
    void bootstrap();
    /// Whether the transport's node is up.
    bool ready() const;

    /// Handle incoming sealed message from transport (called by KithImpl's onReceive).
    void handleReceive(const std::string &topic, const std::string &sealedOnceDecoded);

private:
    // bookId -> encryption key (hex)
    std::map<std::string, std::string> m_activeTopics;
    std::optional<logos_transport::Transport<LogosMap>> m_tx;

    OnEventReceived m_onEvent;
    OnSyncStatus m_onStatus;

    std::string seal(const std::string &bookId, const std::string &plaintext);
    std::optional<std::string> open(const std::string &bookId, const std::string &sealedBytes);
};
