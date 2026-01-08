//! `QObject` deserialization
//!
//! This module implements the [`Deserialize`] trait for `QObject`,
//! allowing it to be created from any serializable format, for
//! example JSON.

use core::fmt;
use std::ffi::CString;

use serde::de::{self, Deserialize, MapAccess, SeqAccess, Visitor};

use super::{to_qobject, QObject};

impl<'de> Deserialize<'de> for QObject {
    #[inline]
    fn deserialize<D>(deserializer: D) -> Result<QObject, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        struct ValueVisitor;

        impl<'de> Visitor<'de> for ValueVisitor {
            type Value = QObject;

            fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
                formatter.write_str("any valid JSON value")
            }

            #[inline]
            fn visit_bool<E>(self, value: bool) -> Result<QObject, E> {
                Ok(value.into())
            }

            #[inline]
            fn visit_i64<E>(self, value: i64) -> Result<QObject, E> {
                Ok(value.into())
            }

            fn visit_i128<E>(self, value: i128) -> Result<QObject, E>
            where
                E: serde::de::Error,
            {
                to_qobject(value).map_err(|_| de::Error::custom("number out of range"))
            }

            #[inline]
            fn visit_u64<E>(self, value: u64) -> Result<QObject, E> {
                Ok(value.into())
            }

            fn visit_u128<E>(self, value: u128) -> Result<QObject, E>
            where
                E: serde::de::Error,
            {
                to_qobject(value).map_err(|_| de::Error::custom("number out of range"))
            }

            #[inline]
            fn visit_f64<E>(self, value: f64) -> Result<QObject, E> {
                Ok(value.into())
            }

            #[inline]
            fn visit_str<E>(self, value: &str) -> Result<QObject, E>
            where
                E: serde::de::Error,
            {
                CString::new(value)
                    .map_err(|_| de::Error::custom("NUL character in string"))
                    .map(QObject::from)
            }

            #[inline]
            fn visit_string<E>(self, value: String) -> Result<QObject, E>
            where
                E: serde::de::Error,
            {
                CString::new(value)
                    .map_err(|_| de::Error::custom("NUL character in string"))
                    .map(QObject::from)
            }

            #[inline]
            fn visit_none<E>(self) -> Result<QObject, E> {
                Ok(().into())
            }

            #[inline]
            fn visit_some<D>(self, deserializer: D) -> Result<QObject, D::Error>
            where
                D: serde::Deserializer<'de>,
            {
                Deserialize::deserialize(deserializer)
            }

            #[inline]
            fn visit_unit<E>(self) -> Result<QObject, E> {
                Ok(().into())
            }

            #[inline]
            fn visit_seq<V>(self, mut visitor: V) -> Result<QObject, V::Error>
            where
                V: SeqAccess<'de>,
            {
                // TODO: insert elements one at a time
                let mut vec = Vec::<QObject>::new();

                while let Some(elem) = visitor.next_element()? {
                    vec.push(elem);
                }
                Ok(QObject::from_iter(vec))
            }

            fn visit_map<V>(self, mut visitor: V) -> Result<QObject, V::Error>
            where
                V: MapAccess<'de>,
            {
                // TODO: insert elements one at a time
                let mut vec = Vec::<(CString, QObject)>::new();

                if let Some(first_key) = visitor.next_key()? {
                    vec.push((first_key, visitor.next_value()?));
                    while let Some((key, value)) = visitor.next_entry()? {
                        vec.push((key, value));
                    }
                }
                Ok(QObject::from_iter(vec))
            }
        }

        deserializer.deserialize_any(ValueVisitor)
    }
}
