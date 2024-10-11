{
  inputs = {
    nixpkgs.url = "nixpkgs/nixpkgs-unstable";
    flake-parts.url = "github:hercules-ci/flake-parts";
    esp-dev.url = "github:mirrexagon/nixpkgs-esp-dev";
    esp-toolchain.url = "github:plietar/nixpkgs-esp-toolchain";
    kicad-parts.url = "github:plietar/kicad-parts";

    esp-dev.inputs.nixpkgs.follows = "nixpkgs";
    esp-toolchain.inputs.nixpkgs.follows = "nixpkgs";
    kicad-parts.inputs.nixpkgs.follows = "nixpkgs";
  };

  outputs = inputs: inputs.flake-parts.lib.mkFlake { inherit inputs; } {
    perSystem = { pkgs, inputs', ... }: {
      devShells.default = pkgs.mkShell {
        buildInputs = [
          (pkgs.python3.withPackages (ps: with ps; [ ps.aiomqtt ps.toml ]))

          (inputs'.esp-dev.packages.esp-idf-full.override {
            toolsToInclude = [ ];
          })
          inputs'.esp-toolchain.packages.riscv32-esp-gcc-20240530
        ];
      };

      devShells.ci = pkgs.mkShell {
        buildInputs = [
          (inputs'.esp-dev.packages.esp-idf-full.override {
            toolsToInclude = [ ];
          })
          inputs'.esp-toolchain.packages.riscv32-esp-gcc-20240530
        ];
      };
    };

    systems = [
      "x86_64-linux"
      "x86_64-darwin"
    ];
  };
}
