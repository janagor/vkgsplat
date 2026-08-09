{
  description = "A C++ flake for cmake_template project";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    # intel-oneapi-vtune is not in nixos-unstable yet; pull from the packaging PR.
    nixpkgs-vtune.url = "github:NixOS/nixpkgs?ref=refs/pull/307132/head";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      nixpkgs,
      nixpkgs-vtune,
      flake-utils,
      ...
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs { inherit system; };
        # VTune is proprietary; only needed on Linux.
        pkgsVtune = import nixpkgs-vtune {
          inherit system;
          config.allowUnfree = true;
        };
        llvm = pkgs.llvmPackages_22;

        llvmStdenv = pkgs.overrideCC llvm.stdenv (
          llvm.stdenv.cc.override {
            bintools = llvm.bintools;
          }
        );

        # VTune GPU hardware metrics need Metrics Discovery API (libigdmd.so).
        # Not shipped with the nix intel-oneapi-vtune package and not in nixpkgs yet.
        intel-metrics-discovery = pkgs.stdenv.mkDerivation rec {
          pname = "intel-metrics-discovery";
          version = "1.14.182";

          src = pkgs.fetchFromGitHub {
            owner = "intel";
            repo = "metrics-discovery";
            rev = "metrics-discovery-${version}";
            hash = "sha256-AgrCJR10B1rtk/VLx7k5I3A4ZVhHoF3p4oxyiY4yAnI=";
          };

          nativeBuildInputs = with pkgs; [
            cmake
            pkg-config
          ];

          buildInputs = with pkgs; [
            libdrm
          ];

          # Upstream writes artifacts under dump/; still installs via GNUInstallDirs.
          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
          ];

          meta = with pkgs.lib; {
            description = "Intel Metrics Discovery API (libigdmd) for GPU performance metrics";
            homepage = "https://github.com/intel/metrics-discovery";
            license = licenses.mit;
            platforms = platforms.linux;
          };
        };

        # Wrap vtune so libigdmd is visible even under `sudo` (which strips LD_LIBRARY_PATH
        # on NixOS despite --preserve-env).
        intel-oneapi-vtune = pkgs.symlinkJoin {
          name = "intel-oneapi-vtune-with-mdapi";
          paths = [ pkgsVtune.intel-oneapi-vtune ];
          nativeBuildInputs = [ pkgs.makeWrapper ];
          postBuild = ''
            wrapProgram "$out/bin/vtune" \
              --prefix LD_LIBRARY_PATH : "${intel-metrics-discovery}/lib"
          '';
        };

        vtune_packages = pkgs.lib.optionals pkgs.stdenv.isLinux [
          intel-oneapi-vtune
          intel-metrics-discovery
        ];

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
        ]
        ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
          mesa
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
          ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
            mesa
            intel-metrics-discovery
          ]
        );

        mesaShellHook = pkgs.lib.optionalString pkgs.stdenv.isLinux ''
          export VK_DRIVER_FILES="$(echo ${pkgs.mesa}/share/vulkan/icd.d/*.json | tr ' ' ':')"
        '';

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
              glslang
            ]
            ++ project_packages
            ++ vtune_packages;

          LD_LIBRARY_PATH = ld_library_path;
          shellHook = mesaShellHook;
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
              glslang

              mold
            ]
            ++ project_packages
            ++ vtune_packages;

          LD_LIBRARY_PATH = ld_library_path;
          shellHook = mesaShellHook;
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
