{ pkgs ? import <nixpkgs> {}
, lib ? pkgs.lib
, stdenv ? pkgs.stdenv

, inNixShell ? false
}:

let
  projectName = "lispico";

  shell = pkgs.mkShell {
    pname = "${projectName}-shell";

    packages = [
      pkgs.gcc
      pkgs.pkg-config
      pkgs.shellcheck

      # For testing scripts
      pkgs.time # For memory footprint measurements and evaluation time
      pkgs.gawk
    ];
  };

  lispico = stdenv.mkDerivation (lib.fix (self: {
    pname = projectName;
    version = "0.0.0";

    meta = {
      description = "A small, explicitly typed Lisp-like programming language interpreter";
      homepage = "https://github.com/unclechu/lispico";
      license = lib.licenses.mit;
      mainProgram = self.pname;
      platforms = lib.platforms.unix;
    };

    src = pkgs.nix-gitignore.gitignoreFilterRecursiveSource (path: type:
      (type == "directory" && builtins.baseNameOf path == "src") ||
      (type == "regular" && (lib.hasSuffix ".c" path || lib.hasSuffix ".h" path))
    ) [] ./.;

    dontConfigure = true;

    nativeBuildInputs = [
      pkgs.pkg-config
    ];

    buildInputs = [
      # …
    ];

    buildPhase = ''
      runHook preBuild
      (
        set -- # Clear "$@" in this subshell
        MAIN_SOURCE_FILE=interpreter.c
        OUTPUT_EXECUTABLE_NAME=${lib.escapeShellArg self.pname}
        ${builtins.readFile ./build.sh}
      )
      runHook postBuild
    '';

    installPhase = ''
      runHook preInstall
      install -Dm755 \
        ${lib.escapeShellArg self.pname} \
        "$out/bin/"${lib.escapeShellArg self.pname}
      runHook postInstall
    '';
  }));
in

(if inNixShell then shell else {}) // {
  inherit shell lispico;
}
