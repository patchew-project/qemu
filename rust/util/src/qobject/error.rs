//! Error data type for `QObject`'s `serde` integration

use std::{
    ffi::NulError,
    fmt::{self, Display},
    str::Utf8Error,
};

use serde::ser;

#[derive(Debug)]
pub enum Error {
    Custom(String),
    KeyMustBeAString,
    InvalidUtf8,
    NulEncountered,
    NumberOutOfRange,
}

impl ser::Error for Error {
    fn custom<T: Display>(msg: T) -> Self {
        Error::Custom(msg.to_string())
    }
}

impl From<NulError> for Error {
    fn from(_: NulError) -> Self {
        Error::NulEncountered
    }
}

impl From<Utf8Error> for Error {
    fn from(_: Utf8Error) -> Self {
        Error::InvalidUtf8
    }
}

impl Display for Error {
    fn fmt(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Error::Custom(msg) => formatter.write_str(msg),
            Error::KeyMustBeAString => formatter.write_str("key must be a string"),
            Error::InvalidUtf8 => formatter.write_str("invalid UTF-8 in string"),
            Error::NulEncountered => formatter.write_str("NUL character in string"),
            Error::NumberOutOfRange => formatter.write_str("number out of range"),
        }
    }
}

impl std::error::Error for Error {}

pub type Result<T> = std::result::Result<T, Error>;
