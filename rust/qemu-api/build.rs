// Copyright 2024 Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0 OR GPL-3.0-or-later

use std::{env, path::Path};

fn main() {
    println!("cargo:rerun-if-env-changed=MESON_BUILD_ROOT");
    println!("cargo:rerun-if-changed=src/bindings.rs.inc");

    let out_dir = env::var_os("OUT_DIR").unwrap();

    if let Some(build_dir) = std::env::var_os("MESON_BUILD_ROOT") {
        let mut build_dir = Path::new(&build_dir).to_path_buf();
        let mut out_dir = Path::new(&out_dir).to_path_buf();
        assert!(
            build_dir.exists(),
            "MESON_BUILD_ROOT value does not exist on filesystem: {}",
            build_dir.display()
        );
        assert!(
            build_dir.is_dir(),
            "MESON_BUILD_ROOT value is not actually a directory: {}",
            build_dir.display()
        );
        // TODO: add logic for other guest target architectures.
        build_dir.push("bindings-aarch64-softmmu.rs");
        let bindings_rs = build_dir;
        assert!(
            bindings_rs.exists(),
            "MESON_BUILD_ROOT/bindings-aarch64-softmmu.rs does not exist on filesystem: {}",
            bindings_rs.display()
        );
        assert!(
            bindings_rs.is_file(),
            "MESON_BUILD_ROOT/bindings-aarch64-softmmu.rs is not a file: {}",
            bindings_rs.display()
        );
        out_dir.push("bindings.rs");
        std::fs::copy(bindings_rs, out_dir).unwrap();
        println!("cargo:rustc-cfg=MESON_BINDINGS_RS");
    } else if !Path::new("src/bindings.rs.inc").exists() {
        panic!(
            "No generated C bindings found! Either build them manually with bindgen or with meson \
             (`ninja bindings-aarch64-softmmu.rs`) and copy them to src/bindings.rs.inc, or build \
             through meson."
        );
    }
}
