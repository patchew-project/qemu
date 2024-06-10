#[cfg(MESON_GENERATED_RS)]
include!(concat!(env!("OUT_DIR"), "/generated.rs"));

#[cfg(not(MESON_GENERATED_RS))]
include!("generated.rs.inc");
