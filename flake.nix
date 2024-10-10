{
  inputs = {
    hyprland.url = "github:hyprwm/Hyprland";
  };

  outputs = {
    self,
    hyprland,
    ...
  }: let
    inherit (hyprland.inputs) nixpkgs;
    eachSystem = nixpkgs.lib.genAttrs (import hyprland.inputs.systems);
    pkgsFor = eachSystem (system: import nixpkgs {localSystem = system;});
    rawCommitPins = (builtins.fromTOML (builtins.readFile ./hyprpm.toml)).repository.commit_pins;
    commitPins = builtins.listToAttrs (
      map (p: {
        name = builtins.head p;
        value = builtins.elemAt p 1;
      })
      rawCommitPins
    );
    srcRev = "${commitPins.${hyprland.rev} or "git"}";
  in {
    packages = eachSystem (system: let
      pkgs = pkgsFor.${system};
    in rec {
      hyprsplit = pkgs.stdenv.mkDerivation {
        pname = "hyprsplit";
        version = "0.1";
        src =
          if (commitPins ? ${hyprland.rev}) && (self ? rev)
          then
            (builtins.fetchGit {
              url = "https://github.com/shezdy/hyprsplit";
              rev = srcRev;
            })
          else ./.;

        nativeBuildInputs = with pkgs; [pkg-config meson ninja];
        buildInputs = with pkgs;
          [
            hyprland.packages.${system}.hyprland.dev
            pixman
            libdrm
          ]
          ++ hyprland.packages.${system}.hyprland.buildInputs;

        meta = with pkgs.lib; {
          homepage = "https://github.com/shezdy/hyprsplit";
          description = "Hyprland plugin for separate sets of workspaces on each monitor";
          license = licenses.bsd3;
          platforms = platforms.linux;
        };
      };

      default = hyprsplit;
    });

    devShells = eachSystem (system: let
      pkgs = pkgsFor.${system};
    in {
      default = pkgs.mkShell.override {stdenv = pkgs.gcc13Stdenv;} {
        shellHook = ''
          meson setup build --reconfigure
          cp ./build/compile_commands.json ./compile_commands.json
        '';
        name = "hyprsplit";
        inputsFrom = [self.packages.${system}.hyprsplit];
      };
    });
  };
}
