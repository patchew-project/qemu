//! `QObject` serializer
//!
//! This module implements a [`Serializer`](serde::ser::Serializer) that
//! produces `QObject`s, allowing them to be created from serializable data
//! structures (such as primitive data types, or structs that implement
//! `Serialize`).

use std::ffi::CString;

use serde::ser::{Impossible, Serialize};

use super::{
    error::{Error, Result},
    QObject,
};

pub struct SerializeTupleVariant {
    name: CString,
    vec: Vec<QObject>,
}

impl serde::ser::SerializeTupleVariant for SerializeTupleVariant {
    type Ok = QObject;
    type Error = Error;

    fn serialize_field<T>(&mut self, value: &T) -> Result<()>
    where
        T: ?Sized + Serialize,
    {
        self.vec.push(to_qobject(value)?);
        Ok(())
    }

    fn end(self) -> Result<QObject> {
        let SerializeTupleVariant { name, vec, .. } = self;

        // TODO: insert elements one at a time
        let list = QObject::from_iter(vec);

        // serde by default represents enums as a single-entry object, with
        // the variant stored in the key ("external tagging").  Internal tagging
        // is implemented by the struct that requests it, not by the serializer.
        let map = [(name, list)];
        Ok(QObject::from_iter(map))
    }
}

pub struct SerializeStructVariant {
    name: CString,
    vec: Vec<(CString, QObject)>,
}

impl serde::ser::SerializeStructVariant for SerializeStructVariant {
    type Ok = QObject;
    type Error = Error;

    fn serialize_field<T>(&mut self, key: &'static str, value: &T) -> Result<()>
    where
        T: ?Sized + Serialize,
    {
        self.vec.push((CString::new(key)?, to_qobject(value)?));
        Ok(())
    }

    fn end(self) -> Result<QObject> {
        // TODO: insert keys one at a time
        let SerializeStructVariant { name, vec, .. } = self;
        let list = QObject::from_iter(vec);

        // serde by default represents enums as a single-entry object, with
        // the variant stored in the key ("external tagging").  Internal tagging
        // is implemented by the struct that requests it, not by the serializer.
        let map = [(name, list)];
        Ok(QObject::from_iter(map))
    }
}

pub struct SerializeVec {
    vec: Vec<QObject>,
}

impl serde::ser::SerializeSeq for SerializeVec {
    type Ok = QObject;
    type Error = Error;

    fn serialize_element<T>(&mut self, value: &T) -> Result<()>
    where
        T: ?Sized + Serialize,
    {
        self.vec.push(to_qobject(value)?);
        Ok(())
    }

    fn end(self) -> Result<QObject> {
        // TODO: insert elements one at a time
        let SerializeVec { vec, .. } = self;
        let list = QObject::from_iter(vec);
        Ok(list)
    }
}

impl serde::ser::SerializeTuple for SerializeVec {
    type Ok = QObject;
    type Error = Error;

    fn serialize_element<T>(&mut self, value: &T) -> Result<()>
    where
        T: ?Sized + Serialize,
    {
        serde::ser::SerializeSeq::serialize_element(self, value)
    }

    fn end(self) -> Result<QObject> {
        serde::ser::SerializeSeq::end(self)
    }
}

impl serde::ser::SerializeTupleStruct for SerializeVec {
    type Ok = QObject;
    type Error = Error;

    fn serialize_field<T>(&mut self, value: &T) -> Result<()>
    where
        T: ?Sized + Serialize,
    {
        serde::ser::SerializeSeq::serialize_element(self, value)
    }

    fn end(self) -> Result<QObject> {
        serde::ser::SerializeSeq::end(self)
    }
}

struct MapKeySerializer;

impl serde::Serializer for MapKeySerializer {
    type Ok = CString;
    type Error = Error;

    type SerializeSeq = Impossible<CString, Error>;
    type SerializeTuple = Impossible<CString, Error>;
    type SerializeTupleStruct = Impossible<CString, Error>;
    type SerializeTupleVariant = Impossible<CString, Error>;
    type SerializeMap = Impossible<CString, Error>;
    type SerializeStruct = Impossible<CString, Error>;
    type SerializeStructVariant = Impossible<CString, Error>;

    #[inline]
    fn serialize_unit_variant(
        self,
        _name: &'static str,
        _variant_index: u32,
        variant: &'static str,
    ) -> Result<CString> {
        Ok(CString::new(variant)?)
    }

    #[inline]
    fn serialize_newtype_struct<T>(self, _name: &'static str, value: &T) -> Result<CString>
    where
        T: ?Sized + Serialize,
    {
        value.serialize(self)
    }

    fn serialize_bool(self, _value: bool) -> Result<CString> {
        Err(Error::KeyMustBeAString)
    }

    fn serialize_i8(self, _value: i8) -> Result<CString> {
        Err(Error::KeyMustBeAString)
    }

    fn serialize_i16(self, _value: i16) -> Result<CString> {
        Err(Error::KeyMustBeAString)
    }

    fn serialize_i32(self, _value: i32) -> Result<CString> {
        Err(Error::KeyMustBeAString)
    }

    fn serialize_i64(self, _value: i64) -> Result<CString> {
        Err(Error::KeyMustBeAString)
    }

    fn serialize_i128(self, _value: i128) -> Result<CString> {
        Err(Error::KeyMustBeAString)
    }

    fn serialize_u8(self, _value: u8) -> Result<CString> {
        Err(Error::KeyMustBeAString)
    }

    fn serialize_u16(self, _value: u16) -> Result<CString> {
        Err(Error::KeyMustBeAString)
    }

    fn serialize_u32(self, _value: u32) -> Result<CString> {
        Err(Error::KeyMustBeAString)
    }

    fn serialize_u64(self, _value: u64) -> Result<CString> {
        Err(Error::KeyMustBeAString)
    }

    fn serialize_u128(self, _value: u128) -> Result<CString> {
        Err(Error::KeyMustBeAString)
    }

    fn serialize_f32(self, _value: f32) -> Result<CString> {
        Err(Error::KeyMustBeAString)
    }

    fn serialize_f64(self, _value: f64) -> Result<CString> {
        Err(Error::KeyMustBeAString)
    }

    #[inline]
    fn serialize_char(self, _value: char) -> Result<CString> {
        Err(Error::KeyMustBeAString)
    }

    #[inline]
    fn serialize_str(self, value: &str) -> Result<CString> {
        Ok(CString::new(value)?)
    }

    fn serialize_bytes(self, value: &[u8]) -> Result<CString> {
        Ok(CString::new(value)?)
    }

    fn serialize_unit(self) -> Result<CString> {
        Err(Error::KeyMustBeAString)
    }

    fn serialize_unit_struct(self, _name: &'static str) -> Result<CString> {
        Err(Error::KeyMustBeAString)
    }

    fn serialize_newtype_variant<T>(
        self,
        _name: &'static str,
        _variant_index: u32,
        _variant: &'static str,
        _value: &T,
    ) -> Result<CString>
    where
        T: ?Sized + Serialize,
    {
        Err(Error::KeyMustBeAString)
    }

    fn serialize_none(self) -> Result<CString> {
        Err(Error::KeyMustBeAString)
    }

    fn serialize_some<T>(self, _value: &T) -> Result<CString>
    where
        T: ?Sized + Serialize,
    {
        Err(Error::KeyMustBeAString)
    }

    fn serialize_seq(self, _len: Option<usize>) -> Result<Self::SerializeSeq> {
        Err(Error::KeyMustBeAString)
    }

    fn serialize_tuple(self, _len: usize) -> Result<Self::SerializeTuple> {
        Err(Error::KeyMustBeAString)
    }

    fn serialize_tuple_struct(
        self,
        _name: &'static str,
        _len: usize,
    ) -> Result<Self::SerializeTupleStruct> {
        Err(Error::KeyMustBeAString)
    }

    fn serialize_tuple_variant(
        self,
        _name: &'static str,
        _variant_index: u32,
        _variant: &'static str,
        _len: usize,
    ) -> Result<Self::SerializeTupleVariant> {
        Err(Error::KeyMustBeAString)
    }

    fn serialize_map(self, _len: Option<usize>) -> Result<Self::SerializeMap> {
        Err(Error::KeyMustBeAString)
    }

    fn serialize_struct(self, _name: &'static str, _len: usize) -> Result<Self::SerializeStruct> {
        Err(Error::KeyMustBeAString)
    }

    fn serialize_struct_variant(
        self,
        _name: &'static str,
        _variant_index: u32,
        _variant: &'static str,
        _len: usize,
    ) -> Result<Self::SerializeStructVariant> {
        Err(Error::KeyMustBeAString)
    }
}

pub struct SerializeMap {
    vec: Vec<(CString, QObject)>,
    next_key: Option<CString>,
}

impl serde::ser::SerializeMap for SerializeMap {
    type Ok = QObject;
    type Error = Error;

    fn serialize_key<T>(&mut self, key: &T) -> Result<()>
    where
        T: ?Sized + Serialize,
    {
        self.next_key = Some(key.serialize(MapKeySerializer)?);
        Ok(())
    }

    fn serialize_value<T>(&mut self, value: &T) -> Result<()>
    where
        T: ?Sized + Serialize,
    {
        let key = self.next_key.take();
        // Panic because this indicates a bug in the program rather than an
        // expected failure.
        let key = key.expect("serialize_value called before serialize_key");
        self.vec.push((key, to_qobject(value)?));
        Ok(())
    }

    fn end(self) -> Result<QObject> {
        // TODO: insert keys one at a time
        let SerializeMap { vec, .. } = self;
        Ok(QObject::from_iter(vec))
    }
}

impl serde::ser::SerializeStruct for SerializeMap {
    type Ok = QObject;
    type Error = Error;

    fn serialize_field<T>(&mut self, key: &'static str, value: &T) -> Result<()>
    where
        T: ?Sized + Serialize,
    {
        serde::ser::SerializeMap::serialize_entry(self, key, value)
    }

    fn end(self) -> Result<QObject> {
        serde::ser::SerializeMap::end(self)
    }
}

/// Serializer whose output is a `QObject`.
///
/// This is the serializer that backs [`to_qobject`].
pub struct Serializer;

impl serde::Serializer for Serializer {
    type Ok = QObject;
    type Error = Error;
    type SerializeSeq = SerializeVec;
    type SerializeTuple = SerializeVec;
    type SerializeTupleStruct = SerializeVec;
    type SerializeTupleVariant = SerializeTupleVariant;
    type SerializeMap = SerializeMap;
    type SerializeStruct = SerializeMap;
    type SerializeStructVariant = SerializeStructVariant;

    #[inline]
    fn serialize_bool(self, value: bool) -> Result<QObject> {
        Ok(value.into())
    }

    #[inline]
    fn serialize_i8(self, value: i8) -> Result<QObject> {
        Ok(value.into())
    }

    #[inline]
    fn serialize_i16(self, value: i16) -> Result<QObject> {
        Ok(value.into())
    }

    #[inline]
    fn serialize_i32(self, value: i32) -> Result<QObject> {
        Ok(value.into())
    }

    fn serialize_i64(self, value: i64) -> Result<QObject> {
        Ok(value.into())
    }

    fn serialize_i128(self, value: i128) -> Result<QObject> {
        if let Ok(value) = u64::try_from(value) {
            Ok(value.into())
        } else if let Ok(value) = i64::try_from(value) {
            Ok(value.into())
        } else {
            Err(Error::NumberOutOfRange)
        }
    }

    #[inline]
    fn serialize_u8(self, value: u8) -> Result<QObject> {
        Ok(value.into())
    }

    #[inline]
    fn serialize_u16(self, value: u16) -> Result<QObject> {
        Ok(value.into())
    }

    #[inline]
    fn serialize_u32(self, value: u32) -> Result<QObject> {
        Ok(value.into())
    }

    #[inline]
    fn serialize_u64(self, value: u64) -> Result<QObject> {
        Ok(value.into())
    }

    fn serialize_u128(self, value: u128) -> Result<QObject> {
        if let Ok(value) = u64::try_from(value) {
            Ok(value.into())
        } else {
            Err(Error::NumberOutOfRange)
        }
    }

    #[inline]
    fn serialize_f32(self, float: f32) -> Result<QObject> {
        Ok(float.into())
    }

    #[inline]
    fn serialize_f64(self, float: f64) -> Result<QObject> {
        Ok(float.into())
    }

    #[inline]
    fn serialize_char(self, value: char) -> Result<QObject> {
        let mut s = String::new();
        s.push(value);
        Ok(CString::new(s)?.into())
    }

    #[inline]
    fn serialize_str(self, value: &str) -> Result<QObject> {
        Ok(CString::new(value)?.into())
    }

    fn serialize_bytes(self, value: &[u8]) -> Result<QObject> {
        // Serialize into a vector of numeric QObjects
        let it = value.iter().copied();
        Ok(QObject::from_iter(it))
    }

    #[inline]
    fn serialize_unit(self) -> Result<QObject> {
        Ok(().into())
    }

    #[inline]
    fn serialize_unit_struct(self, _name: &'static str) -> Result<QObject> {
        self.serialize_unit()
    }

    #[inline]
    fn serialize_unit_variant(
        self,
        _name: &'static str,
        _variant_index: u32,
        variant: &'static str,
    ) -> Result<QObject> {
        self.serialize_str(variant)
    }

    #[inline]
    fn serialize_newtype_struct<T>(self, _name: &'static str, value: &T) -> Result<QObject>
    where
        T: ?Sized + Serialize,
    {
        value.serialize(self)
    }

    fn serialize_newtype_variant<T>(
        self,
        _name: &'static str,
        _variant_index: u32,
        variant: &'static str,
        value: &T,
    ) -> Result<QObject>
    where
        T: ?Sized + Serialize,
    {
        // serde by default represents enums as a single-entry object, with
        // the variant stored in the key ("external tagging").  Internal tagging
        // is implemented by the struct that requests it, not by the serializer.
        let map = [(CString::new(variant)?, to_qobject(value)?)];
        Ok(QObject::from_iter(map))
    }

    #[inline]
    fn serialize_none(self) -> Result<QObject> {
        self.serialize_unit()
    }

    #[inline]
    fn serialize_some<T>(self, value: &T) -> Result<QObject>
    where
        T: ?Sized + Serialize,
    {
        value.serialize(self)
    }

    fn serialize_seq(self, len: Option<usize>) -> Result<Self::SerializeSeq> {
        Ok(SerializeVec {
            vec: Vec::with_capacity(len.unwrap_or(0)),
        })
    }

    fn serialize_tuple(self, len: usize) -> Result<Self::SerializeTuple> {
        self.serialize_seq(Some(len))
    }

    fn serialize_tuple_struct(
        self,
        _name: &'static str,
        len: usize,
    ) -> Result<Self::SerializeTupleStruct> {
        self.serialize_seq(Some(len))
    }

    fn serialize_tuple_variant(
        self,
        _name: &'static str,
        _variant_index: u32,
        variant: &'static str,
        len: usize,
    ) -> Result<Self::SerializeTupleVariant> {
        Ok(SerializeTupleVariant {
            name: CString::new(variant)?,
            vec: Vec::with_capacity(len),
        })
    }

    fn serialize_map(self, _len: Option<usize>) -> Result<Self::SerializeMap> {
        Ok(SerializeMap {
            vec: Vec::new(),
            next_key: None,
        })
    }
    fn serialize_struct(self, _name: &'static str, len: usize) -> Result<Self::SerializeStruct> {
        self.serialize_map(Some(len))
    }

    fn serialize_struct_variant(
        self,
        _name: &'static str,
        _variant_index: u32,
        variant: &'static str,
        _len: usize,
    ) -> Result<Self::SerializeStructVariant> {
        Ok(SerializeStructVariant {
            name: CString::new(variant)?,
            vec: Vec::new(),
        })
    }
}

pub fn to_qobject<T>(input: T) -> Result<QObject>
where
    T: Serialize,
{
    input.serialize(Serializer)
}
