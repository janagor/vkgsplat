{
  description = "A C++ flake for cmake_template project";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      nixpkgs,
      flake-utils,
      ...
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs { inherit system; };
        llvm = pkgs.llvmPackages_22;

        llvmStdenv = pkgs.overrideCC llvm.stdenv (
          llvm.stdenv.cc.override {
            bintools = llvm.bintools;
          }
        );

        clangShell = pkgs.mkShell.override { stdenv = llvmStdenv; } {
          packages = with pkgs; [
            cmake
            ninja
            gcovr
            ccache
            doxygen
            cppcheck
            graphviz
            pkg-config
            include-what-you-use
            llvm.clang-tools
          ];
        };

        gccShell = pkgs.mkShell.override { stdenv = pkgs.gcc16Stdenv; } {
          packages = with pkgs; [
            cmake
            ninja
            gcovr
            ccache
            doxygen
            cppcheck
            graphviz
            pkg-config
            include-what-you-use
            llvm.clang-tools

            mold
          ];
        };
      in
      {
        devShells = {
          default = clangShell;
          gcc = gccShell;
          clang = clangShell;
        };
      }
    );
}
