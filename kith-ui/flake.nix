{
  description = "Kith address-book UI — pure-QML view over the kith core module (kith ADR 0006).";

  inputs = {
    # Pin to the SAME builder rev scala-ui/flake.lock resolves (verified via
    # `nix flake metadata` on scala-ui: locked rev afe4430ee6eb7ba45c08a516a43e18500720c715,
    # manifestVersion "0.3.0"), so the view builds against a known-installable SDK rev
    # and Basecamp will install the .lgx. `kith.inputs.logos-module-builder.follows`
    # cascades the SAME rev into the kith core (and, through it, loam_core) so view +
    # core + identity dep share one SDK — avoids the cross-module IPC skew a mismatched
    # builder rev can cause (kith ADR 0006: "same version-skew discipline as Scala").
    logos-module-builder.url = "github:logos-co/logos-module-builder/afe4430ee6eb7ba45c08a516a43e18500720c715";
    kith.url = "path:../";
    kith.inputs.logos-module-builder.follows = "logos-module-builder";
  };

  outputs = inputs@{ logos-module-builder, kith, ... }:
    logos-module-builder.lib.mkLogosQmlModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
