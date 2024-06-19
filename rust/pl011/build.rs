use std::{env, path::Path};

fn main() {
    println!("cargo::rerun-if-env-changed=MESON_BUILD_DIR");
    println!("cargo::rerun-if-env-changed=MESON_BUILD_ROOT");
    println!("cargo::rerun-if-changed=src/generated.rs.inc");

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
        build_dir.push("aarch64-softmmu-generated.rs");
        let generated_rs = build_dir;
        assert!(
            generated_rs.exists(),
            "MESON_BUILD_ROOT/aarch64-softmmu-generated.rs does not exist on filesystem: {}",
            generated_rs.display()
        );
        assert!(
            generated_rs.is_file(),
            "MESON_BUILD_ROOT/aarch64-softmmu-generated.rs is not a file: {}",
            generated_rs.display()
        );
        out_dir.push("generated.rs");
        std::fs::copy(generated_rs, out_dir).unwrap();
        println!("cargo::rustc-cfg=MESON_GENERATED_RS");
    } else if !Path::new("src/generated.rs.inc").exists() {
        panic!(
            "No generated C bindings found! Either build them manually with bindgen or with meson \
             (`ninja aarch64-softmmu-generated.rs`) and copy them to src/generated.rs.inc, or build through meson."
        );
    }
}
