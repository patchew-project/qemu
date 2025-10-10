//! `QObject` deserializer
//!
//! This module implements a [`Deserializer`](serde::de::Deserializer) that
//! produces `QObject`s, allowing them to be turned into deserializable data
//! structures (such as primitive data types, or structs that implement
//! `Deserialize`).

use std::ffi::CStr;

use serde::de::{
    self, value::StrDeserializer, DeserializeSeed, Expected, IntoDeserializer, MapAccess,
    SeqAccess, Unexpected, Visitor,
};

use super::{
    error::{Error, Result},
    match_qobject, QObject,
};
use crate::bindings;

impl QObject {
    #[cold]
    fn invalid_type<E>(&self, exp: &dyn Expected) -> E
    where
        E: serde::de::Error,
    {
        serde::de::Error::invalid_type(self.unexpected(), exp)
    }

    #[cold]
    fn unexpected(&self) -> Unexpected<'_> {
        match_qobject! { (self) =>
            () => Unexpected::Unit,
            bool(b) => Unexpected::Bool(b),
            i64(n) => Unexpected::Signed(n),
            u64(n) => Unexpected::Unsigned(n),
            f64(n) => Unexpected::Float(n),
            CStr(s) => s.to_str().map_or_else(
                |_| Unexpected::Other("string with invalid UTF-8"),
                Unexpected::Str),
            QList(_) => Unexpected::Seq,
            QDict(_) => Unexpected::Map,
        }
    }
}

fn visit_qlist_ref<'de, V>(qlist: &'de bindings::QList, visitor: V) -> Result<V::Value>
where
    V: Visitor<'de>,
{
    struct QListDeserializer(*mut bindings::QListEntry, usize);

    impl<'de> SeqAccess<'de> for QListDeserializer {
        type Error = Error;

        fn next_element_seed<T>(&mut self, seed: T) -> Result<Option<T::Value>>
        where
            T: DeserializeSeed<'de>,
        {
            if self.0.is_null() {
                return Ok(None);
            }

            let e = unsafe { &*self.0 };
            // increment the reference count because deserialize consumes `value`.
            let value = unsafe { QObject::cloned_from_raw(e.value.cast_const()) };
            let result = seed.deserialize(value);
            self.0 = unsafe { e.next.tqe_next };
            self.1 += 1;
            result.map(Some)
        }
    }

    let mut deserializer = QListDeserializer(unsafe { qlist.head.tqh_first }, 0);
    let seq = visitor.visit_seq(&mut deserializer)?;
    if deserializer.0.is_null() {
        Ok(seq)
    } else {
        Err(serde::de::Error::invalid_length(
            deserializer.1,
            &"fewer elements in array",
        ))
    }
}

fn visit_qdict_ref<'de, V>(qdict: &'de bindings::QDict, visitor: V) -> Result<V::Value>
where
    V: Visitor<'de>,
{
    struct QDictDeserializer(*mut bindings::QDict, *const bindings::QDictEntry);

    impl<'de> MapAccess<'de> for QDictDeserializer {
        type Error = Error;

        fn next_key_seed<T>(&mut self, seed: T) -> Result<Option<T::Value>>
        where
            T: DeserializeSeed<'de>,
        {
            if self.1.is_null() {
                return Ok(None);
            }

            let e = unsafe { &*self.1 };
            let key = unsafe { CStr::from_ptr(e.key) };
            let key_de = StrDeserializer::new(key.to_str()?);
            seed.deserialize(key_de).map(Some)
        }

        fn next_value_seed<T>(&mut self, seed: T) -> Result<T::Value>
        where
            T: DeserializeSeed<'de>,
        {
            if self.1.is_null() {
                panic!("next_key must have returned None");
            }

            let e = unsafe { &*self.1 };
            // increment the reference count because deserialize consumes `value`.
            let value = unsafe { QObject::cloned_from_raw(e.value) };
            let result = seed.deserialize(value);
            self.1 = unsafe { bindings::qdict_next(self.0, self.1) };
            result
        }
    }

    let qdict = (qdict as *const bindings::QDict).cast_mut();
    let e = unsafe { bindings::qdict_first(qdict) };
    let mut deserializer = QDictDeserializer(qdict, e);
    let map = visitor.visit_map(&mut deserializer)?;
    if deserializer.1.is_null() {
        Ok(map)
    } else {
        Err(serde::de::Error::invalid_length(
            unsafe { bindings::qdict_size(qdict) },
            &"fewer elements in map",
        ))
    }
}

fn visit_qnum_ref<'de, V>(qnum: QObject, visitor: V) -> Result<V::Value>
where
    V: Visitor<'de>,
{
    match_qobject! { (qnum) =>
        i64(n) => visitor.visit_i64(n),
        u64(n) => visitor.visit_u64(n),
        f64(n) => visitor.visit_f64(n),
        _ => Err(qnum.invalid_type(&"number")),
    }
}

macro_rules! deserialize_number {
    ($method:ident) => {
        fn $method<V>(self, visitor: V) -> Result<V::Value>
        where
            V: Visitor<'de>,
        {
            visit_qnum_ref(self, visitor)
        }
    };
}

impl<'de> serde::Deserializer<'de> for QObject {
    type Error = Error;

    fn deserialize_any<V>(self, visitor: V) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        match_qobject! { (self) =>
            () => visitor.visit_unit(),
            bool(v) => visitor.visit_bool(v),
            i64(n) => visitor.visit_i64(n),
            u64(n) => visitor.visit_u64(n),
            f64(n) => visitor.visit_f64(n),
            CStr(cstr) => visitor.visit_str(cstr.to_str()?),
            QList(qlist) => visit_qlist_ref(qlist, visitor),
            QDict(qdict) => visit_qdict_ref(qdict, visitor),
        }
    }

    deserialize_number!(deserialize_i8);
    deserialize_number!(deserialize_i16);
    deserialize_number!(deserialize_i32);
    deserialize_number!(deserialize_i64);
    deserialize_number!(deserialize_i128);
    deserialize_number!(deserialize_u8);
    deserialize_number!(deserialize_u16);
    deserialize_number!(deserialize_u32);
    deserialize_number!(deserialize_u64);
    deserialize_number!(deserialize_u128);
    deserialize_number!(deserialize_f32);
    deserialize_number!(deserialize_f64);

    fn deserialize_option<V>(self, visitor: V) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        match_qobject! { (self) =>
            () => visitor.visit_none(),
            _ => visitor.visit_some(self),
        }
    }

    fn deserialize_enum<V>(
        self,
        _name: &'static str,
        _variants: &'static [&'static str],
        visitor: V,
    ) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        match_qobject! { (self) =>
            CStr(cstr) => visitor.visit_enum(cstr.to_str()?.into_deserializer()),
            _ => Err(self.invalid_type(&"string")),
        }
    }

    fn deserialize_newtype_struct<V>(self, _name: &'static str, visitor: V) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        visitor.visit_newtype_struct(self)
    }

    fn deserialize_bool<V>(self, visitor: V) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        match_qobject! { (self) =>
            bool(v) => visitor.visit_bool(v),
            _ => Err(self.invalid_type(&visitor)),
        }
    }

    fn deserialize_char<V>(self, visitor: V) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        self.deserialize_str(visitor)
    }

    fn deserialize_str<V>(self, visitor: V) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        match_qobject! { (self) =>
            CStr(cstr) => visitor.visit_str(cstr.to_str()?),
            _ => Err(self.invalid_type(&visitor)),
        }
    }

    fn deserialize_string<V>(self, visitor: V) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        self.deserialize_str(visitor)
    }

    fn deserialize_bytes<V>(self, visitor: V) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        match_qobject! { (self) =>
            CStr(cstr) => visitor.visit_str(cstr.to_str()?),
            QList(qlist) => visit_qlist_ref(qlist, visitor),
            _ => Err(self.invalid_type(&visitor)),
        }
    }

    fn deserialize_byte_buf<V>(self, visitor: V) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        self.deserialize_bytes(visitor)
    }

    fn deserialize_unit<V>(self, visitor: V) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        match_qobject! { (self) =>
            () => visitor.visit_unit(),
            _ => Err(self.invalid_type(&visitor)),
        }
    }

    fn deserialize_unit_struct<V>(self, _name: &'static str, visitor: V) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        self.deserialize_unit(visitor)
    }

    fn deserialize_seq<V>(self, visitor: V) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        match_qobject! { (self) =>
            QList(qlist) => visit_qlist_ref(qlist, visitor),
            _ => Err(self.invalid_type(&visitor)),
        }
    }

    fn deserialize_tuple<V>(self, _len: usize, visitor: V) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        self.deserialize_seq(visitor)
    }

    fn deserialize_tuple_struct<V>(
        self,
        _name: &'static str,
        _len: usize,
        visitor: V,
    ) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        self.deserialize_seq(visitor)
    }

    fn deserialize_map<V>(self, visitor: V) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        match_qobject! { (self) =>
            QDict(qdict) => visit_qdict_ref(qdict, visitor),
            _ => Err(self.invalid_type(&visitor)),
        }
    }

    fn deserialize_struct<V>(
        self,
        _name: &'static str,
        _fields: &'static [&'static str],
        visitor: V,
    ) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        match_qobject! { (self) =>
            QList(qlist) => visit_qlist_ref(qlist, visitor),
            QDict(qdict) => visit_qdict_ref(qdict, visitor),
            _ => Err(self.invalid_type(&visitor)),
        }
    }

    fn deserialize_identifier<V>(self, visitor: V) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        self.deserialize_str(visitor)
    }

    fn deserialize_ignored_any<V>(self, visitor: V) -> Result<V::Value>
    where
        V: Visitor<'de>,
    {
        visitor.visit_unit()
    }
}

pub fn from_qobject<T>(value: QObject) -> Result<T>
where
    T: de::DeserializeOwned,
{
    T::deserialize(value)
}
