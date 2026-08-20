Vendored from github.com/vpavlin/logos-sync @ 0.2.0 (basecamp/logos_sync/), copied via
vpavlin/scala's own vendoring (scala/src/logos_sync/). Do not edit here - change it
upstream and re-vendor. See that repo's docs/adr/ for the design.

Phase 4 (multi-device sync): catchup.hpp + reconcile.hpp are now vendored too (copied
byte-for-byte from vpavlin/scala's src/logos_sync/, which itself vendors them from
logos-sync 0.2.0 upstream). They back ContactSync's SYNC_REQ catch-up path
(contact_sync.h/.cpp + KithImpl::onSyncReq/sendSyncReq), 1:1 with
scala_impl.cpp's onSyncReq/sendSyncReq. Do not edit here - change it upstream (or in
scala, which is kept as the reference mirror) and re-vendor.
