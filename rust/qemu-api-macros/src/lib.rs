// Copyright 2024, Linaro Limited
// Author(s): Manos Pitsidianakis <manos.pitsidianakis@linaro.org>
// SPDX-License-Identifier: GPL-2.0-or-later

use proc_macro::TokenStream;
use quote::{quote, quote_spanned, ToTokens};
use syn::{
    parse::Parse, parse_macro_input, parse_quote, punctuated::Punctuated, spanned::Spanned,
    token::Comma, Data, DeriveInput, Field, Fields, FieldsUnnamed, Ident, Meta, Path, Token,
    Variant,
};

mod utils;
use utils::MacroError;

fn get_fields<'a>(
    input: &'a DeriveInput,
    msg: &str,
) -> Result<&'a Punctuated<Field, Comma>, MacroError> {
    let Data::Struct(ref s) = &input.data else {
        return Err(MacroError::Message(
            format!("Struct required for {msg}"),
            input.ident.span(),
        ));
    };
    let Fields::Named(ref fs) = &s.fields else {
        return Err(MacroError::Message(
            format!("Named fields required for {msg}"),
            input.ident.span(),
        ));
    };
    Ok(&fs.named)
}

fn get_unnamed_field<'a>(input: &'a DeriveInput, msg: &str) -> Result<&'a Field, MacroError> {
    let Data::Struct(ref s) = &input.data else {
        return Err(MacroError::Message(
            format!("Struct required for {msg}"),
            input.ident.span(),
        ));
    };
    let Fields::Unnamed(FieldsUnnamed { ref unnamed, .. }) = &s.fields else {
        return Err(MacroError::Message(
            format!("Tuple struct required for {msg}"),
            s.fields.span(),
        ));
    };
    if unnamed.len() != 1 {
        return Err(MacroError::Message(
            format!("A single field is required for {msg}"),
            s.fields.span(),
        ));
    }
    Ok(&unnamed[0])
}

fn is_c_repr(input: &DeriveInput, msg: &str) -> Result<(), MacroError> {
    let expected = parse_quote! { #[repr(C)] };

    if input.attrs.iter().any(|attr| attr == &expected) {
        Ok(())
    } else {
        Err(MacroError::Message(
            format!("#[repr(C)] required for {msg}"),
            input.ident.span(),
        ))
    }
}

fn is_transparent_repr(input: &DeriveInput, msg: &str) -> Result<(), MacroError> {
    let expected = parse_quote! { #[repr(transparent)] };

    if input.attrs.iter().any(|attr| attr == &expected) {
        Ok(())
    } else {
        Err(MacroError::Message(
            format!("#[repr(transparent)] required for {msg}"),
            input.ident.span(),
        ))
    }
}

fn derive_object_or_error(input: DeriveInput) -> Result<proc_macro2::TokenStream, MacroError> {
    is_c_repr(&input, "#[derive(Object)]")?;

    let name = &input.ident;
    let parent = &get_fields(&input, "#[derive(Object)]")?[0].ident;

    Ok(quote! {
        ::qemu_api::assert_field_type!(#name, #parent,
            ::qemu_api::qom::ParentField<<#name as ::qemu_api::qom::ObjectImpl>::ParentType>);

        ::qemu_api::module_init! {
            MODULE_INIT_QOM => unsafe {
                ::qemu_api::bindings::type_register_static(&<#name as ::qemu_api::qom::ObjectImpl>::TYPE_INFO);
            }
        }
    })
}

#[proc_macro_derive(Object)]
pub fn derive_object(input: TokenStream) -> TokenStream {
    let input = parse_macro_input!(input as DeriveInput);
    let expanded = derive_object_or_error(input).unwrap_or_else(Into::into);

    TokenStream::from(expanded)
}

fn derive_opaque_or_error(input: DeriveInput) -> Result<proc_macro2::TokenStream, MacroError> {
    is_transparent_repr(&input, "#[derive(Wrapper)]")?;

    let name = &input.ident;
    let field = &get_unnamed_field(&input, "#[derive(Wrapper)]")?;
    let typ = &field.ty;

    // TODO: how to add "::qemu_api"?  For now, this is only used in the
    // qemu_api crate so it's not a problem.
    Ok(quote! {
        unsafe impl crate::cell::Wrapper for #name {
            type Wrapped = <#typ as crate::cell::Wrapper>::Wrapped;
        }
        impl #name {
            pub unsafe fn from_raw<'a>(ptr: *mut <Self as crate::cell::Wrapper>::Wrapped) -> &'a Self {
                let ptr = ::std::ptr::NonNull::new(ptr).unwrap().cast::<Self>();
                unsafe { ptr.as_ref() }
            }

            pub const fn as_mut_ptr(&self) -> *mut <Self as crate::cell::Wrapper>::Wrapped {
                self.0.as_mut_ptr()
            }

            pub const fn as_ptr(&self) -> *const <Self as crate::cell::Wrapper>::Wrapped {
                self.0.as_ptr()
            }

            pub const fn as_void_ptr(&self) -> *mut ::core::ffi::c_void {
                self.0.as_void_ptr()
            }

            pub const fn raw_get(slot: *mut Self) -> *mut <Self as crate::cell::Wrapper>::Wrapped {
                slot.cast()
            }
        }
    })
}

#[derive(Debug)]
struct DeviceProperty {
    name: Option<syn::LitCStr>,
    qdev_prop: Option<syn::Path>,
    assert_type: Option<proc_macro2::TokenStream>,
    bitnr: Option<syn::Expr>,
    defval: Option<syn::Expr>,
}

impl Parse for DeviceProperty {
    fn parse(input: syn::parse::ParseStream) -> syn::Result<Self> {
        let _: syn::Token![#] = input.parse()?;
        let bracketed;
        _ = syn::bracketed!(bracketed in input);
        let attribute = bracketed.parse::<syn::Ident>()?.to_string();
        let (assert_type, qdev_prop) = match attribute.as_str() {
            "property" => (None, None),
            "bool_property" => (
                Some(quote! { bool }),
                Some(syn::parse2(
                    quote! { ::qemu_api::bindings::qdev_prop_bool },
                )?),
            ),
            other => unreachable!("Got unexpected DeviceProperty attribute `{}`", other),
        };
        let mut retval = Self {
            name: None,
            qdev_prop,
            assert_type,
            bitnr: None,
            defval: None,
        };
        let content;
        _ = syn::parenthesized!(content in bracketed);
        while !content.is_empty() {
            let value: syn::Ident = content.parse()?;
            if value == "name" {
                let _: syn::Token![=] = content.parse()?;
                if retval.name.is_some() {
                    panic!("`name` can only be used at most once");
                }
                retval.name = Some(content.parse()?);
            } else if value == "qdev_prop" {
                let _: syn::Token![=] = content.parse()?;
                if retval.assert_type.is_some() {
                    // qdev_prop will be Some(_), but we want to print a helpful error message
                    // explaining why you should use #[property(...)] instead of saying "you
                    // defined qdev_prop twice".
                    panic!("Use `property` attribute instead of `{attribute}` if you want to override `qdev_prop` value.");
                }
                if retval.qdev_prop.is_some() {
                    panic!("`qdev_prop` can only be used at most once");
                }
                retval.qdev_prop = Some(content.parse()?);
            } else if value == "bitnr" {
                let _: syn::Token![=] = content.parse()?;
                if retval.bitnr.is_some() {
                    panic!("`bitnr` can only be used at most once");
                }
                retval.bitnr = Some(content.parse()?);
            } else if value == "default" {
                let _: syn::Token![=] = content.parse()?;
                if retval.defval.is_some() {
                    panic!("`default` can only be used at most once");
                }
                retval.defval = Some(content.parse()?);
            } else {
                panic!("unrecognized field `{value}`");
            }

            if !content.is_empty() {
                let _: syn::Token![,] = content.parse()?;
            }
        }
        Ok(retval)
    }
}

#[proc_macro_derive(DeviceProperties, attributes(property, bool_property))]
pub fn derive_device_properties(input: TokenStream) -> TokenStream {
    let span = proc_macro::Span::call_site();
    let input = parse_macro_input!(input as DeriveInput);
    let properties: Vec<(syn::Field, proc_macro2::Span, DeviceProperty)> = match input.data {
        syn::Data::Struct(syn::DataStruct {
            fields: syn::Fields::Named(fields),
            ..
        }) => fields
            .named
            .iter()
            .flat_map(|f| {
                f.attrs
                    .iter()
                    .filter(|a| a.path().is_ident("property") || a.path().is_ident("bool_property"))
                    .map(|a| {
                        (
                            f.clone(),
                            f.span(),
                            syn::parse(a.to_token_stream().into())
                                .expect("could not parse property attr"),
                        )
                    })
            })
            .collect::<Vec<_>>(),
        _other => unreachable!(),
    };
    let name = &input.ident;

    let mut assertions = vec![];
    let mut properties_expanded = vec![];
    let zero = syn::Expr::Verbatim(quote! { 0 });
    for (field, field_span, prop) in properties {
        let prop_name = prop.name.as_ref().unwrap();
        let field_name = field.ident.as_ref().unwrap();
        let qdev_prop = prop.qdev_prop.as_ref().unwrap();
        let bitnr = prop.bitnr.as_ref().unwrap_or(&zero);
        let set_default = prop.defval.is_some();
        let defval = prop.defval.as_ref().unwrap_or(&zero);
        if let Some(assert_type) = prop.assert_type {
            assertions.push(quote_spanned! {field_span=>
                ::qemu_api::assert_field_type! ( #name, #field_name, #assert_type );
            });
        }
        properties_expanded.push(quote_spanned! {field_span=>
            ::qemu_api::bindings::Property {
                // use associated function syntax for type checking
                name: ::std::ffi::CStr::as_ptr(#prop_name),
                info: unsafe { &#qdev_prop },
                offset: ::core::mem::offset_of!(#name, #field_name) as isize,
                bitnr: #bitnr,
                set_default: #set_default,
                defval: ::qemu_api::bindings::Property__bindgen_ty_1 { u: #defval as u64 },
                ..::qemu_api::zeroable::Zeroable::ZERO
            }
        });
    }
    let properties_expanded = quote_spanned! {span.into()=>
        #(#assertions)*

        impl ::qemu_api::qdev::DevicePropertiesImpl for #name {
            fn properties() -> &'static [::qemu_api::bindings::Property] {
                static PROPERTIES: &'static [::qemu_api::bindings::Property] = &[#(#properties_expanded),*];

                PROPERTIES
            }
        }
    };

    TokenStream::from(properties_expanded)
}

#[proc_macro_derive(Wrapper)]
pub fn derive_opaque(input: TokenStream) -> TokenStream {
    let input = parse_macro_input!(input as DeriveInput);
    let expanded = derive_opaque_or_error(input).unwrap_or_else(Into::into);

    TokenStream::from(expanded)
}

#[allow(non_snake_case)]
fn get_repr_uN(input: &DeriveInput, msg: &str) -> Result<Path, MacroError> {
    let repr = input.attrs.iter().find(|attr| attr.path().is_ident("repr"));
    if let Some(repr) = repr {
        let nested = repr.parse_args_with(Punctuated::<Meta, Token![,]>::parse_terminated)?;
        for meta in nested {
            match meta {
                Meta::Path(path) if path.is_ident("u8") => return Ok(path),
                Meta::Path(path) if path.is_ident("u16") => return Ok(path),
                Meta::Path(path) if path.is_ident("u32") => return Ok(path),
                Meta::Path(path) if path.is_ident("u64") => return Ok(path),
                _ => {}
            }
        }
    }

    Err(MacroError::Message(
        format!("#[repr(u8/u16/u32/u64) required for {msg}"),
        input.ident.span(),
    ))
}

fn get_variants(input: &DeriveInput) -> Result<&Punctuated<Variant, Comma>, MacroError> {
    let Data::Enum(ref e) = &input.data else {
        return Err(MacroError::Message(
            "Cannot derive TryInto for union or struct.".to_string(),
            input.ident.span(),
        ));
    };
    if let Some(v) = e.variants.iter().find(|v| v.fields != Fields::Unit) {
        return Err(MacroError::Message(
            "Cannot derive TryInto for enum with non-unit variants.".to_string(),
            v.fields.span(),
        ));
    }
    Ok(&e.variants)
}

#[rustfmt::skip::macros(quote)]
fn derive_tryinto_or_error(input: DeriveInput) -> Result<proc_macro2::TokenStream, MacroError> {
    let repr = get_repr_uN(&input, "#[derive(TryInto)]")?;

    let name = &input.ident;
    let variants = get_variants(&input)?;
    let discriminants: Vec<&Ident> = variants.iter().map(|f| &f.ident).collect();

    Ok(quote! {
        impl core::convert::TryFrom<#repr> for #name {
            type Error = #repr;

            fn try_from(value: #repr) -> Result<Self, Self::Error> {
                #(const #discriminants: #repr = #name::#discriminants as #repr;)*;
                match value {
                    #(#discriminants => Ok(Self::#discriminants),)*
                    _ => Err(value),
                }
            }
        }
    })
}

#[proc_macro_derive(TryInto)]
pub fn derive_tryinto(input: TokenStream) -> TokenStream {
    let input = parse_macro_input!(input as DeriveInput);
    let expanded = derive_tryinto_or_error(input).unwrap_or_else(Into::into);

    TokenStream::from(expanded)
}
