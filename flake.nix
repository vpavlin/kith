{
  description = "kith engine + contact-book CORE module (identity via loam_core, ADR 0006).";
  inputs = {
    # port/0.3: builder 0.3.1; kith rides the loam_core facade on UPSTREAM delivery v0.3.0.
    logos-module-builder.url = "github:logos-co/logos-module-builder/0.3.1";
    loam_core.url = "github:vpavlin/loam-basecamp/553253fee586baeb16d84c77e3f6da543ba7de9b?dir=core";
  };
  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
