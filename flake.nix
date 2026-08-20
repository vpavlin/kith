{
  description = "kith engine + contact-book CORE module (identity via loam_core, ADR 0006).";
  inputs = {
    # kith routes identity/signing through the loam_core FACADE (not delivery_module
    # directly), the same shape scala uses (scala ADR 0015 / kith ADR 0003 "Loam owns
    # WHO"). Pinned to the exact revs scala/flake.lock resolved at scaffold time, so
    # kith builds against the identical SDK and avoids the manifestVersion-0.2.0 trap
    # a moving branch tag (e.g. logos-module-builder/0.2.6) can reintroduce.
    loam_core.url = "github:vpavlin/loam-basecamp/24758d7acf5e8e3bfa77f935754d4e0129e5c017?dir=core";
    logos-module-builder.url = "github:logos-co/logos-module-builder/2b59cb8e855894f7e7a064b15bfae409f288080b";
    loam_core.inputs.logos-module-builder.follows = "logos-module-builder";
  };
  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
