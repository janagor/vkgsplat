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

        project_packages = with pkgs; [
          libXi
          libX11
          libXrandr
          libXcursor
          libXinerama

          libffi
          libxkbcommon
          wayland
          wayland-scanner

          vulkan-loader
        ];

        ld_library_path = pkgs.lib.makeLibraryPath (
          with pkgs;
          [
            libXi
            libX11
            libXrandr
            libXcursor
            libXinerama

            libxkbcommon
            wayland

            vulkan-loader
          ]
        );

        clangShell = pkgs.mkShell.override { stdenv = llvmStdenv; } {
          packages =
            with pkgs;
            [
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
            ]
            ++ project_packages;

          LD_LIBRARY_PATH = ld_library_path;
        };

        gccShell = pkgs.mkShell.override { stdenv = pkgs.gcc16Stdenv; } {
          packages =
            with pkgs;
            [
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
            ]
            ++ project_packages;

          LD_LIBRARY_PATH = ld_library_path;
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
