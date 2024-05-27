/*
 * Pretty-printer for simple trace backend binary trace files (Rust version)
 *
 * Copyright (C) 2024 Intel Corporation.
 *
 * Authors: Zhao Liu <zhao1.liu@intel.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#![allow(dead_code)]
#![allow(unused_variables)]

mod trace;

use std::env;
use std::fs::File;
use std::io::Error as IOError;
use std::io::ErrorKind;
use std::io::Read;

use clap::Arg;
use clap::Command;
use thiserror::Error;
use trace::Event;

const RECORD_TYPE_MAPPING: u64 = 0;
const RECORD_TYPE_EVENT: u64 = 1;

#[derive(Error, Debug)]
pub enum Error
{
    #[error("usage: {0} [--no-header] <trace-events> <trace-file>")]
    CliOptionUnmatch(String),
    #[error("Failed to read file: {0}")]
    ReadFile(IOError),
    #[error("Unknown record type ({0})")]
    UnknownRecType(u64),
}

pub type Result<T> = std::result::Result<T, Error>;

enum RecordType
{
    Empty,
    Mapping,
    Event,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct RecordRawType
{
    rtype: u64,
}

impl RecordType
{
    fn read_type(mut fobj: &File) -> Result<RecordType>
    {
        let mut tbuf = [0u8; 8];
        if let Err(e) = fobj.read_exact(&mut tbuf) {
            if e.kind() == ErrorKind::UnexpectedEof {
                return Ok(RecordType::Empty);
            } else {
                return Err(Error::ReadFile(e));
            }
        }

        /*
         * Safe because the layout of the trace record requires us to parse
         * the type first, and then there is a check on the validity of the
         * record type.
         */
        let raw_t =
            unsafe { std::mem::transmute::<[u8; 8], RecordRawType>(tbuf) };
        match raw_t.rtype {
            RECORD_TYPE_MAPPING => Ok(RecordType::Mapping),
            RECORD_TYPE_EVENT => Ok(RecordType::Event),
            _ => Err(Error::UnknownRecType(raw_t.rtype)),
        }
    }
}

trait ReadHeader
{
    fn read_header(fobj: &File) -> Result<Self>
    where
        Self: Sized;
}

#[repr(C)]
#[derive(Clone, Copy)]
struct LogHeader
{
    event_id: u64,
    magic: u64,
    version: u64,
}

impl ReadHeader for LogHeader
{
    fn read_header(mut fobj: &File) -> Result<Self>
    {
        let mut raw_hdr = [0u8; 24];
        fobj.read_exact(&mut raw_hdr).map_err(Error::ReadFile)?;

        /*
         * Safe because the size of log header (struct LogHeader)
         * is 24 bytes, which is ensured by simple trace backend.
         */
        let hdr =
            unsafe { std::mem::transmute::<[u8; 24], LogHeader>(raw_hdr) };
        Ok(hdr)
    }
}

#[derive(Default)]
struct RecordInfo
{
    event_id: u64,
    timestamp_ns: u64,
    record_pid: u32,
    args_payload: Vec<u8>,
}

impl RecordInfo
{
    fn new() -> Self
    {
        Default::default()
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
struct RecordHeader
{
    event_id: u64,
    timestamp_ns: u64,
    record_length: u32,
    record_pid: u32,
}

impl RecordHeader
{
    fn extract_record(&self, mut fobj: &File) -> Result<RecordInfo>
    {
        let mut info = RecordInfo::new();

        info.event_id = self.event_id;
        info.timestamp_ns = self.timestamp_ns;
        info.record_pid = self.record_pid;
        info.args_payload = vec![
            0u8;
            self.record_length as usize
                - std::mem::size_of::<RecordHeader>()
        ];
        fobj.read_exact(&mut info.args_payload)
            .map_err(Error::ReadFile)?;

        Ok(info)
    }
}

impl ReadHeader for RecordHeader
{
    fn read_header(mut fobj: &File) -> Result<Self>
    {
        let mut raw_hdr = [0u8; 24];
        fobj.read_exact(&mut raw_hdr).map_err(Error::ReadFile)?;

        /*
         * Safe because the size of record header (struct RecordHeader)
         * is 24 bytes, which is ensured by simple trace backend.
         */
        let hdr: RecordHeader =
            unsafe { std::mem::transmute::<[u8; 24], RecordHeader>(raw_hdr) };
        Ok(hdr)
    }
}

pub struct EventArgPayload {}

trait Analyzer
{
    /* Called at the start of the trace. */
    fn begin(&self) {}

    /* Called if no specific method for processing a trace event. */
    fn catchall(
        &mut self,
        rec_args: &[EventArgPayload],
        event: &Event,
        timestamp_ns: u64,
        pid: u32,
        event_id: u64,
    ) -> Result<String>;

    /* Called at the end of the trace. */
    fn end(&self) {}

    /*
     * TODO: Support "variable" parameters (i.e. variants of process_event()
     * with different parameters, like **kwargs in python), when we need a
     * simpletrace rust module.
     */
    fn process_event(
        &mut self,
        rec_args: &[EventArgPayload],
        event: &Event,
        event_id: u64,
        timestamp_ns: u64,
        pid: u32,
    ) -> Result<String>
    {
        self.catchall(rec_args, event, timestamp_ns, pid, event_id)

        /*
         * TODO: Support custom function hooks (like getattr() in python),
         * when we need a simpletrace rust module.
         */
    }
}

struct Formatter
{
    last_timestamp_ns: Option<u64>,
}

impl Formatter
{
    fn new() -> Self
    {
        Formatter {
            last_timestamp_ns: None,
        }
    }
}

impl Analyzer for Formatter
{
    fn catchall(
        &mut self,
        rec_args: &[EventArgPayload],
        event: &Event,
        timestamp_ns: u64,
        pid: u32,
        event_id: u64,
    ) -> Result<String>
    {
        let fmt_str = String::new();

        Ok(fmt_str)
    }
}

fn process(
    event_path: &str,
    trace_path: &str,
    analyzer: &mut Formatter,
    read_header: bool,
) -> Result<()>
{
    analyzer.begin();
    analyzer.end();

    Ok(())
}

/*
 * Execute an analyzer on a trace file given on the command-line.
 * This function is useful as a driver for simple analysis scripts.  More
 * advanced scripts will want to call process() instead.
 */
fn run(analyzer: &mut Formatter) -> Result<()>
{
    let matches = Command::new("simple trace")
        .arg(
            Arg::new("no-header")
                .required(false)
                .long("no-header")
                .action(clap::ArgAction::SetTrue)
                .help("Disable header parsing"),
        )
        .arg(
            Arg::new("trace-events")
                .required(true)
                .action(clap::ArgAction::Set)
                .help("Path to trace events file"),
        )
        .arg(
            Arg::new("trace-file")
                .required(true)
                .action(clap::ArgAction::Set)
                .help("Path to trace file"),
        )
        .try_get_matches()
        .map_err(|_| {
            Error::CliOptionUnmatch(
                env::current_exe()
                    .unwrap_or_else(|_| "simpletrace".into())
                    .to_string_lossy()
                    .to_string(),
            )
        })?;

    let no_header = matches.get_flag("no-header");
    let event_path = matches.get_one::<String>("trace-events").unwrap();
    let trace_path = matches.get_one::<String>("trace-file").unwrap();

    process(event_path, trace_path, analyzer, !no_header)?;

    Ok(())
}

fn main()
{
    let mut fmt = Formatter::new();

    if let Err(e) = run(&mut fmt) {
        println!("{:?}", e.to_string());
    }
}
