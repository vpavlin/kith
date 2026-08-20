#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "kith_engine.hpp"
#include "kith_identity.hpp"   // per-device secp256k1 signing identity (fallback signer)
#include <logos_module_context.h>  // LogosModuleContext base + logos_events: + modules()

// Forward declaration for the internal store component (ported to std types).
class ContactStore;

/**
 * KithImpl - Universal-pattern core module for the Kith address-book app (kith ADR
 * 0006: "kith" core + a future "kith_ui" view, mirroring scala's core/view split).
 *
 * Pure C++ class - no Qt, no Q_OBJECT, no Q_PLUGIN_METADATA.
 * All public methods are auto-exposed by logos-cpp-generator.
 * Inter-module calls via modules().loam_core.* (auto-generated typed SDK) - the
 * identity seam (kith ADR 0003: "Loam owns WHO"). Kith never depends on any other
 * ecosystem app; the dependency points Kith -> Loam only.
 * Events declared below with a logos_events: section.
 *
 * v1 SCOPE (this core, this pass): the contact CRDT (kith_engine's fold + the
 * ContactStore log/registry) and the app-facing query API (kith ADR 0007) -
 * createBook/listBooks/deleteBook, addContact/editContact/deleteContact/listContacts,
 * findByAddress/searchContacts/addAuthorToContacts, and a MINIMAL vCard
 * import/export (kith ADR 0005 - see src/vcard.hpp's own TODO list for exactly what
 * it does not yet handle).
 *
 * OUT OF SCOPE for this core (left for later, following scala as the template when
 * it's time):
 *   - Sync/transport (kith ADR 0004: loam-transport over the shared node, SDS
 *     Reliable Channels). A book's log is written and folded LOCALLY ONLY right now;
 *     there is no CalendarSync-equivalent yet, so a book does not (yet) sync across
 *     your own devices, let alone with another person. Add it the way
 *     scala_impl.cpp's onContextReady()/CalendarSync does - one content topic per
 *     book (`/kith/1/<bookId>/...` per ADR 0006), vendoring
 *     logos_sync/catchup.hpp + reconcile.hpp for RBSR catch-up.
 *   - pickContact() (kith ADR 0007) - the shared contact-picker UI/intent other
 *     apps call. That is a VIEW concern (kith_ui) once it exists, not this core's.
 *   - Keycard-signed writes (kith ADR 0008) - deliberately deferred; the identity
 *     path here only goes through loam_core's DEVICE/SOFT identities
 *     (identityForContainer/signDigest), never keycardSign/enrollKeycard. Wire it up
 *     the way scala_impl.cpp's authorEvent/authorAndPublish/enrollKeycard do.
 *   - Avatars (kith ADR 0002/0006) - the Contact shape allows an "avatar" field but
 *     nothing here bounds/downscales/stores one; treat it as an opaque string for now.
 */
class KithImpl : public LogosModuleContext {
public:
    KithImpl();
    ~KithImpl() override;

    /// Core build version - keep in lockstep with metadata.json "version". A future
    /// kith_ui view compares this to the minimum it needs (scala's version-skew
    /// discipline, kith ADR 0006).
    std::string coreVersion() const;

    // -- Identity API (device-local fallback signer; loam_core is authoritative) --
    /// The device's own fallback signing address (used only when a book has no
    /// loam_core identity bound yet - see mkEvent).
    std::string getIdentity() const;

    // -- Book CRUD (kith ADR 0004: one log per book; personal book = single-writer
    //    for now, shared books are a later phase) --------------------------------
    /// Create a new address book. identityId binds a specific loam_core identity as
    /// the book's authoring identity (kith ADR 0003); empty = the current default.
    /// Returns the book ID.
    std::string createBook(const std::string& name, const std::string& identityId = "");
    /// List all books. Returns a JSON array string.
    std::string listBooks();
    /// Delete a book and all its contacts.
    bool deleteBook(const std::string& id);

    // -- Contact CRUD (kith ADR 0002: whole-record contact.set / contact.del) -----
    /// Add a contact. contactJson is the Contact record (kith ADR 0002 shape) minus
    /// "id" - the id is assigned here. Returns the new contact ID.
    std::string addContact(const std::string& bookId, const std::string& contactJson);
    /// Replace an existing contact's whole record. contactJson MUST include "id".
    /// Returns the contact ID.
    std::string editContact(const std::string& bookId, const std::string& contactJson);
    /// Delete (tombstone) a contact. Terminal - see kith_engine.hpp's fold rule.
    bool deleteContact(const std::string& bookId, const std::string& contactId);
    /// List all contacts in a book, folded to current state. Returns a JSON array.
    std::string listContacts(const std::string& bookId);
    /// Get a single contact by id, searching every book. Returns a JSON object
    /// string ("{}" if not found).
    std::string getContact(const std::string& contactId);

    // -- App-facing query API (kith ADR 0007) -------------------------------------
    /// Look up a contact by its loamIdentity.address, across all books - how a
    /// consuming app resolves "who wrote this" back to a display name. Returns a
    /// JSON object string ("{}" if not found).
    std::string findByAddress(const std::string& address);
    /// Search contacts by name/email/phone/handle across all books. Returns a JSON
    /// array string. (A stand-in for the real pickContact() UI, which belongs to
    /// kith_ui - see the class doc's OUT OF SCOPE list.)
    std::string searchContacts(const std::string& query);
    /// The "add author to contacts" flywheel (kith ADR 0007): capture a minimal
    /// contact from an event's author {address, pubHex, name?}. Always lands with
    /// verified:false / addedVia:"event-author" (trust is out-of-band, kith ADR
    /// 0003) and is deduplicated by address within the target book. bookId empty =
    /// use (or create) a personal book named "Contacts". Returns the contact ID.
    std::string addAuthorToContacts(const std::string& bookId, const std::string& name,
                                    const std::string& address, const std::string& pubHex);

    // -- vCard interop (kith ADR 0005) - see src/vcard.hpp for exactly what is and
    //    is not handled (MINIMAL: no line folding, no PHOTO, first TYPE= only) ----
    /// Import one or more vCards (a single .vcf blob) into a book. Returns JSON
    /// {"imported": n, "ids": [...]}
    std::string importVcard(const std::string& bookId, const std::string& vcardText);
    /// Export a book (or a single contact if contactId is set) as a vCard 4.0 blob.
    std::string exportVcard(const std::string& bookId, const std::string& contactId = "");

    // -- Context lifecycle ---------------------------------------------------------
    /// Called when the module context is fully initialized (deps are live).
    void onContextReady() override;

    // -- Events - emitted to subscribers via the host's eventResponse channel ------
logos_events:
    /// Emitted when a book's contacts change (create/edit/delete). NOTE (scala's
    /// documented basecamp 0.2.0 behaviour): module events are not always delivered
    /// to QML - a future kith_ui view should poll listBooks()/listContacts() and
    /// treat this as a best-effort nudge, not the source of truth.
    void bookChanged(const std::string& bookId);

    /// Emitted when the local device identity changes.
    void identityChanged();

private:
    ContactStore* m_store = nullptr;
    std::string m_identity;      // == m_signId.address - device fallback author id
    kith::SignId m_signId;       // secp256k1 keypair; private key persisted in the kv store
    bool m_ctxReady = false;     // onContextReady() actually fired

    // -- event-log CRDT helpers (mirrors scala_impl's mkEvent/publishAndApply) ----
    long long m_wall = 0;        // HLC clock
    long long m_ctr = 0;
    kith::HLC nextHlc();
    kith::Event mkEvent(const std::string& type, const kith::json& payload, const std::string& bookId = "");
    // Persist locally (sync/broadcast is not wired up yet - see class doc).
    void publishAndApply(const std::string& bookId, const kith::Event& e);

    // Find which book currently holds a given contact id (linear scan over every
    // book's fold) - used by getContact()/deleteContact() callers that only have an
    // id. Returns "" if not found in any book.
    std::string bookOwning(const std::string& contactId) const;
};
