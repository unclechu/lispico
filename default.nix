{ pkgs ? import <nixpkgs> {}
, lib ? pkgs.lib
, stdenv ? pkgs.stdenv

, inNixShell ? false
}:

let
  shell = pkgs.mkShell {
    packages = [
      pkgs.gcc
      pkgs.pkg-config
    ];
  };

  simple-lisp-interpreter = stdenv.mkDerivation (lib.fix (self: {
    pname = "simple-lisp-interpreter";
    meta.mainProgram = self.pname;
    version = "0.0.0";

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
  inherit shell simple-lisp-interpreter;
}
