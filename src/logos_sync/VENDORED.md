Vendored from github.com/vpavlin/logos-sync @ 0.2.0 (basecamp/logos_sync/), copied via
vpavlin/scala's own vendoring (scala/src/logos_sync/). Do not edit here - change it
upstream and re-vendor. See that repo's docs/adr/ for the design.

Kith v1 uses only event + merge (the envelope + the union-by-id/HLC-sort CRDT merge) -
enough for the local fold (kith_engine.hpp). catchup.hpp/reconcile.hpp (RBSR delta
catch-up) are NOT vendored yet because this core does not wire up sync/transport yet
(see kith_impl.h's class doc + the README "Status" section this ADR-implementation
report leaves behind) - add them when Kith gets a CalendarSync-equivalent, following
scala/src/calendar_sync.cpp as the template.
