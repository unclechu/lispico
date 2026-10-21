{ pkgs ? import <nixpkgs> {}
, lib ? pkgs.lib
, stdenv ? pkgs.stdenv

, inNixShell ? false
}:

let
  shell = pkgs.mkShell {
    nativeBuildInputs = [
      pkgs.gcc
      pkgs.pkg-config
    ];

    buildInputs = [
      # …
    ];
  };

  simple-lisp-c-interpreter = stdenv.mkDerivation (lib.fix (self: {
    pname = "simple-lisp-c-interpreter";
    meta.mainProgram = self.pname;
    version = "0.0.0";

    src = ./main.c; # todo

    dontUnpack = true;
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
        MAIN_SOURCE_FILE=$src
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
  inherit shell simple-lisp-c-interpreter;
}
