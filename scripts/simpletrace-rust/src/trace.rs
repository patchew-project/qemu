/*
 * Machinery for generating tracing-related intermediate files (Rust version)
 *
 * Copyright (C) 2024 Intel Corporation.
 *
 * Authors: Zhao Liu <zhao1.liu@intel.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#![allow(dead_code)]

use std::fs::read_to_string;

use once_cell::sync::Lazy;
use regex::Regex;
use thiserror::Error;

#[derive(Error, Debug)]
pub enum Error
{
    #[error("Empty argument (did you forget to use 'void'?)")]
    EmptyArg,
    #[error("Event '{0}' has more than maximum permitted argument count")]
    InvalidArgCnt(String),
    #[error("{0} does not end with a new line")]
    InvalidEventFile(String),
    #[error("Invalid format: {0}")]
    InvalidFormat(String),
    #[error(
        "Argument type '{0}' is not allowed. \
        Only standard C types and fixed size integer \
        types should be used. struct, union, and \
        other complex pointer types should be \
        declared as 'void *'"
    )]
    InvalidType(String),
    #[error("Error at {0}:{1}: {2}")]
    ReadEventFail(String, usize, String),
    #[error("Unknown event: {0}")]
    UnknownEvent(String),
    #[error("Unknown properties: {0}")]
    UnknownProp(String),
}

pub type Result<T> = std::result::Result<T, Error>;

/*
 * Refer to the description of ALLOWED_TYPES in
 * scripts/tracetool/__init__.py.
 */
const ALLOWED_TYPES: [&str; 20] = [
    "int",
    "long",
    "short",
    "char",
    "bool",
    "unsigned",
    "signed",
    "int8_t",
    "uint8_t",
    "int16_t",
    "uint16_t",
    "int32_t",
    "uint32_t",
    "int64_t",
    "uint64_t",
    "void",
    "size_t",
    "ssize_t",
    "uintptr_t",
    "ptrdiff_t",
];

const STRING_TYPES: [&str; 4] =
    ["const char*", "char*", "const char *", "char *"];

/* TODO: Support 'vcpu' property. */
const VALID_PROPS: [&str; 1] = ["disable"];

fn validate_c_type(arg_type: &str) -> Result<()>
{
    static RE_TYPE: Lazy<Regex> = Lazy::new(|| Regex::new(r"\*").unwrap());
    let bits: Vec<String> =
        arg_type.split_whitespace().map(|s| s.to_string()).collect();
    for bit in bits {
        let res = RE_TYPE.replace_all(&bit, "");
        if res.is_empty() {
            continue;
        }
        if res == "const" {
            continue;
        }
        if !ALLOWED_TYPES.contains(&res.as_ref()) {
            return Err(Error::InvalidType(res.to_string()));
        }
    }
    Ok(())
}

pub fn read_events(fname: &str) -> Result<Vec<Event>>
{
    let fstr = read_to_string(fname).unwrap();
    let lines = fstr.lines().map(|s| s.trim()).collect::<Vec<_>>();
    let mut events = Vec::new();

    /*
     * lines() in Rust: Line terminators are not included in the lines
     * returned by the iterator, so check whether line_str is empty.
     */
    for (lineno, line_str) in lines.iter().enumerate() {
        if line_str.is_empty() || line_str.starts_with('#') {
            continue;
        }

        let event = Event::build(line_str, lineno as u32 + 1, fname);
        if let Err(e) = event {
            return Err(Error::ReadEventFail(
                fname.to_owned(),
                lineno,
                e.to_string(),
            ));
        } else {
            events.push(event.unwrap());
        }
    }

    Ok(events)
}

#[derive(Clone)]
pub struct ArgProperty
{
    pub c_type: String,
    pub name: String,
}

impl ArgProperty
{
    fn new(c_type: &str, name: &str) -> Self
    {
        ArgProperty {
            c_type: c_type.to_string(),
            name: name.to_string(),
        }
    }

    pub fn is_string(&self) -> bool
    {
        let arg_strip = self.c_type.trim_start();
        STRING_TYPES.iter().any(|&s| arg_strip.starts_with(s))
            && arg_strip.matches('*').count() == 1
    }
}

#[derive(Default, Clone)]
pub struct Arguments
{
    /* List of (type, name) tuples or arguments properties. */
    pub props: Vec<ArgProperty>,
}

impl Arguments
{
    pub fn new() -> Self
    {
        Arguments { props: Vec::new() }
    }

    pub fn len(&self) -> usize
    {
        self.props.len()
    }

    pub fn build(arg_str: &str) -> Result<Arguments>
    {
        let mut args = Arguments::new();
        for arg in arg_str.split(',').map(|s| s.trim()) {
            if arg.is_empty() {
                return Err(Error::EmptyArg);
            }

            if arg == "void" {
                continue;
            }

            let (arg_type, identifier) = if arg.contains('*') {
                /* FIXME: Implement rsplit_inclusive(). */
                let p = arg.rfind('*').unwrap();
                (
                    /* Safe because arg contains "*" and p exists. */
                    unsafe { arg.get_unchecked(..p + 1) },
                    /* Safe because arg contains "*" and p exists. */
                    unsafe { arg.get_unchecked(p + 1..) },
                )
            } else {
                arg.rsplit_once(' ').unwrap()
            };

            validate_c_type(arg_type)?;
            args.props.push(ArgProperty::new(arg_type, identifier));
        }
        Ok(args)
    }
}

/* TODO: Support original, event_trans, event_exec if needed. */
#[derive(Default, Clone)]
pub struct Event
{
    /* The event name. */
    pub name: String,
    /* Properties of the event. */
    pub properties: Vec<String>,
    /* The event format string. */
    pub fmt: Vec<String>,
    /* The event arguments. */
    pub args: Arguments,
    /* The line number in the input file. */
    pub lineno: u32,
    /* The path to the input file. */
    pub filename: String,
}

impl Event
{
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        name: &str,
        mut props: Vec<String>,
        fmt: Vec<String>,
        args: Arguments,
        lineno: u32,
        filename: &str,
    ) -> Result<Self>
    {
        let mut event = Event {
            name: name.to_string(),
            fmt: fmt.clone(),
            args,
            lineno,
            filename: filename.to_string(),
            ..Default::default()
        };

        event.properties.append(&mut props);

        if event.args.len() > 10 {
            return Err(Error::InvalidArgCnt(event.name));
        }

        let unknown_props: Vec<String> = event
            .properties
            .iter()
            .filter_map(|p| {
                if !VALID_PROPS.contains(&p.as_str()) {
                    Some(p.to_string())
                } else {
                    None
                }
            })
            .collect();
        if !unknown_props.is_empty() {
            return Err(Error::UnknownProp(format!("{:?}", unknown_props)));
        }

        if event.fmt.len() > 2 {
            return Err(Error::InvalidFormat(
                format!("too many arguments ({})", event.fmt.len()).to_string(),
            ));
        }

        Ok(event)
    }

    pub fn build(line_str: &str, lineno: u32, filename: &str) -> Result<Event>
    {
        static RE: Lazy<Regex> = Lazy::new(|| {
            Regex::new(
                r#"(?x)
                ((?P<props>[\w\s]+)\s+)?
                (?P<name>\w+)
                \((?P<args>[^)]*)\)
                \s*
                (?:(?:(?P<fmt_trans>".+),)?\s*(?P<fmt>".+))?
                \s*"#,
            )
            .unwrap()
        });

        let caps_res = RE.captures(line_str);
        if caps_res.is_none() {
            return Err(Error::UnknownEvent(line_str.to_owned()));
        }
        let caps = caps_res.unwrap();
        let name = caps.name("name").map_or("", |m| m.as_str());
        let props: Vec<String> = if caps.name("props").is_some() {
            caps.name("props")
                .unwrap()
                .as_str()
                .split_whitespace()
                .map(|s| s.to_string())
                .collect()
        } else {
            Vec::new()
        };
        let fmt: String =
            caps.name("fmt").map_or("", |m| m.as_str()).to_string();
        let fmt_trans: String = caps
            .name("fmt_trans")
            .map_or("", |m| m.as_str())
            .to_string();

        if fmt.contains("%m") || fmt_trans.contains("%m") {
            return Err(Error::InvalidFormat(
                "Event format '%m' is forbidden, pass the error 
                as an explicit trace argument"
                    .to_string(),
            ));
        }
        if fmt.ends_with(r"\n") {
            return Err(Error::InvalidFormat(
                "Event format must not end with a newline 
                character"
                    .to_string(),
            ));
        }
        let mut fmt_vec = vec![fmt];
        if !fmt_trans.is_empty() {
            fmt_vec.push(fmt_trans);
        }

        let args =
            Arguments::build(caps.name("args").map_or("", |m| m.as_str()))?;
        let event = Event::new(name, props, fmt_vec, args, lineno, filename)?;

        Ok(event)
    }
}
