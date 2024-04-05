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
  in {
    packages = eachSystem (system: let
      pkgs = pkgsFor.${system};
    in rec {
      hyprsplit = pkgs.stdenv.mkDerivation {
        pname = "hyprsplit";
        version = "0.1";
        src = ./.;

        nativeBuildInputs = with pkgs; [pkg-config];
        buildInputs = with pkgs;
          [
            hyprland.packages.${system}.hyprland.dev
            pixman
            libdrm
          ]
          ++ hyprland.packages.${system}.hyprland.buildInputs;

        installPhase = ''
          runHook preInstall

          mkdir -p $out/lib
          mv hyprsplit.so $out/lib/libhyprsplit.so

          runHook postInstall
        '';

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
      default = pkgs.gcc13Stdenv.mkShell {
        name = "hyprsplit";
        inputsFrom = [self.packages.${system}.hyprsplit];
      };
    });
  };
}
