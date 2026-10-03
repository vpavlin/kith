{
  description = "Kith address-book UI — pure-QML view over the kith core module (kith ADR 0006).";

  inputs = {
    # port/0.3: builder 0.3.1 — the same builder as the kith core and loam_core (one SDK).
    logos-module-builder.url = "github:logos-co/logos-module-builder/0.3.1";
    kith.url = "github:vpavlin/kith/ed776654c07eb7ddd9978398b2f551c5ba4a24d7";
    # The view also calls loam_core directly (identities), so it declares it.
    loam_core.url = "github:vpavlin/loam-basecamp/553253fee586baeb16d84c77e3f6da543ba7de9b?dir=core";
  };

  outputs = inputs@{ logos-module-builder, kith, ... }:
    logos-module-builder.lib.mkLogosQmlModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
