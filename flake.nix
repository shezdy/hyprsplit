{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    hyprland.url = "github:hyprwm/Hyprland";
  };

  outputs =
    {
      self,
      hyprland,
      ...
    }:
    let
      inherit (hyprland.inputs) nixpkgs;
      eachSystem = nixpkgs.lib.genAttrs nixpkgs.lib.systems.flakeExposed;
      pkgsFor = eachSystem (system: import nixpkgs { localSystem = system; });
      rawCommitPins = (builtins.fromTOML (builtins.readFile ./hyprpm.toml)).repository.commit_pins;
      commitPins = builtins.listToAttrs (
        map (p: {
          name = builtins.head p;
          value = builtins.elemAt p 1;
        }) rawCommitPins
      );
      srcRev = "${commitPins.${hyprland.rev} or "git"}";
      srcRevShort = builtins.substring 0 7 srcRev;
    in
    {
      packages = eachSystem (
        system:
        let
          pkgs = pkgsFor.${system};
        in
        rec {
          hyprsplit = pkgs.stdenv.mkDerivation {
            pname = "hyprsplit";
            version = "flakeRev=${self.shortRev or "dirty"}_srcRev=${srcRevShort}";
            src =
              if (commitPins ? ${hyprland.rev}) && (self ? rev) then
                (builtins.fetchGit {
                  url = "https://github.com/shezdy/hyprsplit";
                  rev = srcRev;
                })
              else
                ./.;

            nativeBuildInputs = with pkgs; [
              pkg-config
              meson
              ninja
              gcc14
            ];
            buildInputs =
              with pkgs;
              [
                hyprland.packages.${system}.hyprland.dev
                pixman
                libdrm
              ]
              ++ hyprland.packages.${system}.hyprland.buildInputs;

            postInstall = ''
              install -Dm644 $src/init.lua $out/share/hyprsplit/init.lua
            '';

            meta = with pkgs.lib; {
              homepage = "https://github.com/shezdy/hyprsplit";
              description = "Hyprland plugin for separate sets of workspaces on each monitor";
              license = licenses.bsd3;
              platforms = platforms.linux;
            };
          };

          hyprsplitlua = pkgs.runCommand "hyprsplit" { } ''
            mkdir -p $out/share/hyprsplit
            cp ${./.}/init.lua $out/share/hyprsplit/init.lua
          '';

          default = hyprsplit;
        }
      );

      devShells = eachSystem (
        system:
        let
          pkgs = pkgsFor.${system};
        in
        {
          default = pkgs.mkShell.override { stdenv = pkgs.gcc14Stdenv; } {
            shellHook = ''
              meson setup build --reconfigure
              cp ./build/compile_commands.json ./compile_commands.json
            '';
            name = "hyprsplit";
            inputsFrom = [ self.packages.${system}.hyprsplit ];
          };
          luaDev = pkgs.mkShell {
            name = "hyprsplit";
            buildInputs = [
              (pkgs.lua.withPackages (
                ps: with ps; [
                  luacheck # static analysis
                  busted # unit tests
                  inspect # pretty-print
                ]
              ))
              pkgs.luarocks
              pkgs.stylua
            ];
            shellHook = ''
              echo "Lua development shell ready! $(lua -v 2>&1 | head -1)"
            '';
          };
        }
      );
    };
}
