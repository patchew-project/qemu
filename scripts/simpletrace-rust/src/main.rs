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

use std::collections::HashMap;
use std::env;
use std::fs::File;
use std::io::Error as IOError;
use std::io::ErrorKind;
use std::io::Read;
use std::mem::size_of;

use backtrace::Backtrace;
use clap::Arg;
use clap::Command;
use thiserror::Error;
use trace::Event;

const DROPPED_EVENT_ID: u64 = 0xfffffffffffffffe;
const HEADER_MAGIC: u64 = 0xf2b177cb0aa429b4;
const HEADER_EVENT_ID: u64 = 0xffffffffffffffff;

const RECORD_TYPE_MAPPING: u64 = 0;
const RECORD_TYPE_EVENT: u64 = 1;

#[derive(Error, Debug)]
pub enum Error
{
    #[error("usage: {0} [--no-header] <trace-events> <trace-file>")]
    CliOptionUnmatch(String),
    #[error("Invalid event name ({0})")]
    InvalidEventName(String),
    #[error("Not a valid trace file, header id {0} != {1}")]
    InvalidHeaderId(u64, u64),
    #[error("Not a valid trace file, header magic {0} != {1}")]
    InvalidHeaderMagic(u64, u64),
    #[error("Failed to read file: {0}")]
    ReadFile(IOError),
    #[error(
        "event {0} is logged but is not declared in the trace events \
        file, try using trace-events-all instead."
    )]
    UnknownEvent(String),
    #[error("Unknown record type ({0})")]
    UnknownRecType(u64),
    #[error("Unknown version {0} of tracelog format!")]
    UnknownVersion(u64),
    #[error("Log format {0} not supported with this QEMU release!")]
    UnsupportedVersion(u64),
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

const LOG_HDR_LEN: usize = size_of::<LogHeader>();

impl ReadHeader for LogHeader
{
    fn read_header(mut fobj: &File) -> Result<Self>
    {
        let mut raw_hdr = [0u8; LOG_HDR_LEN];
        fobj.read_exact(&mut raw_hdr).map_err(Error::ReadFile)?;

        /*
         * Safe because the size of log header (struct LogHeader)
         * is 24 bytes, which is ensured by simple trace backend.
         */
        let hdr = unsafe {
            std::mem::transmute::<[u8; LOG_HDR_LEN], LogHeader>(raw_hdr)
        };
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

const REC_HDR_LEN: usize = size_of::<RecordHeader>();

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
        let mut raw_hdr = [0u8; REC_HDR_LEN];
        fobj.read_exact(&mut raw_hdr).map_err(Error::ReadFile)?;

        /*
         * Safe because the size of record header (struct RecordHeader)
         * is 24 bytes, which is ensured by simple trace backend.
         */
        let hdr: RecordHeader = unsafe {
            std::mem::transmute::<[u8; REC_HDR_LEN], RecordHeader>(raw_hdr)
        };
        Ok(hdr)
    }
}

#[derive(Clone)]
pub struct EventArgPayload
{
    raw_val: Option<u64>,
    raw_str: Option<String>,
}

impl EventArgPayload
{
    fn new(raw_val: Option<u64>, raw_str: Option<String>) -> Self
    {
        EventArgPayload { raw_val, raw_str }
    }

    fn get_payload_str(
        offset: &mut usize,
        args_payload: &[u8],
    ) -> Result<EventArgPayload>
    {
        let length = u32::from_le_bytes(
            args_payload[*offset..(*offset + 4)].try_into().unwrap(),
        );
        *offset += 4;
        let raw_str: &[u8] = args_payload
            .get(*offset..(*offset + length as usize))
            .unwrap();
        *offset += length as usize;
        Ok(EventArgPayload::new(
            None,
            Some(String::from_utf8_lossy(raw_str).into_owned()),
        ))
    }

    fn get_payload_val(
        offset: &mut usize,
        args_payload: &[u8],
    ) -> Result<EventArgPayload>
    {
        let raw_val = u64::from_le_bytes(
            args_payload[*offset..(*offset + 8)].try_into().unwrap(),
        );
        *offset += 8;
        Ok(EventArgPayload::new(Some(raw_val), None))
    }
}

#[derive(Clone)]
struct EventEntry
{
    event: Event,
    event_id: u64,
    timestamp_ns: u64,
    record_pid: u32,
    rec_args: Vec<EventArgPayload>,
}

impl EventEntry
{
    fn new(
        event: &Event,
        event_id: u64,
        timestamp_ns: u64,
        record_pid: u32,
    ) -> Self
    {
        EventEntry {
            event: event.clone(),
            event_id,
            timestamp_ns,
            record_pid,
            rec_args: Vec::new(),
        }
    }
}

fn get_mapping(mut fobj: &File) -> Result<(u64, String)>
{
    let mut event_id_buf = [0u8; 8];
    fobj.read_exact(&mut event_id_buf)
        .map_err(Error::ReadFile)?;
    let event_id = u64::from_le_bytes(event_id_buf);

    let mut len_buf = [0u8; 4];
    fobj.read_exact(&mut len_buf).map_err(Error::ReadFile)?;

    let len = u32::from_le_bytes(len_buf);
    let mut name_buf = vec![0u8; len as usize];
    fobj.read_exact(&mut name_buf).map_err(Error::ReadFile)?;
    let name = String::from_utf8(name_buf.clone())
        .map_err(|_| Error::InvalidEventName(format!("{:?}", name_buf)))?;

    Ok((event_id, name))
}

fn read_record(fobj: &File) -> Result<RecordInfo>
{
    let hdr = RecordHeader::read_header(fobj)?;
    let info = hdr.extract_record(fobj)?;
    Ok(info)
}

fn read_trace_header(fobj: &File) -> Result<()>
{
    let log_hdr = LogHeader::read_header(fobj)?;
    if log_hdr.event_id != HEADER_EVENT_ID {
        return Err(Error::InvalidHeaderId(log_hdr.event_id, HEADER_EVENT_ID));
    }
    if log_hdr.magic != HEADER_MAGIC {
        return Err(Error::InvalidHeaderMagic(log_hdr.magic, HEADER_MAGIC));
    }
    if ![0, 2, 3, 4].contains(&(log_hdr.version as i64)) {
        return Err(Error::UnknownVersion(log_hdr.version));
    }
    if log_hdr.version != 4 {
        return Err(Error::UnsupportedVersion(log_hdr.version));
    }
    Ok(())
}

fn read_trace_records(
    events: &Vec<Event>,
    fobj: &File,
    analyzer: &mut Formatter,
    read_header: bool,
) -> Result<Vec<String>>
{
    /* backtrace::Backtrace needs this env variable. */
    env::set_var("RUST_BACKTRACE", "1");
    let bt = Backtrace::new();
    let raw_frame = bt.frames().first().unwrap();
    let frameinfo = raw_frame.symbols().first().unwrap();

    let dropped_event = Event::build(
        "Dropped_Event(uint64_t num_events_dropped)",
        frameinfo.lineno().unwrap() + 1,
        frameinfo.filename().unwrap().to_str().unwrap(),
    )
    .unwrap();

    let mut event_mapping = HashMap::new();
    for e in events {
        event_mapping.insert(e.name.clone(), e.clone());
    }

    let drop_str = "dropped".to_string();
    event_mapping.insert(drop_str.clone(), dropped_event.clone());
    let mut event_id_to_name: HashMap<u64, String> = HashMap::new();
    event_id_to_name.insert(DROPPED_EVENT_ID, drop_str.clone());

    if !read_header {
        for (event_id, event) in events.iter().enumerate() {
            event_id_to_name.insert(event_id as u64, event.name.clone());
        }
    }

    let mut fmt_strs = Vec::new();
    loop {
        let rtype = RecordType::read_type(fobj)?;
        match rtype {
            RecordType::Empty => {
                break;
            }
            RecordType::Mapping => {
                let (event_id, event_name) =
                    get_mapping(fobj).expect("Error reading mapping");
                event_id_to_name.insert(event_id, event_name);
                continue;
            }
            RecordType::Event => {
                let rec = read_record(fobj).expect("Error reading record");
                let event_name =
                    event_id_to_name.get(&rec.event_id).unwrap().to_string();
                let event = event_mapping
                    .get(&event_name)
                    .ok_or(Error::UnknownEvent(event_name))?;

                let mut entry = EventEntry::new(
                    event,
                    rec.event_id,
                    rec.timestamp_ns,
                    rec.record_pid,
                );
                let mut offset = 0;

                for arg in &event.args.props {
                    let payload = if arg.is_string() {
                        EventArgPayload::get_payload_str(
                            &mut offset,
                            &rec.args_payload,
                        )?
                    } else {
                        EventArgPayload::get_payload_val(
                            &mut offset,
                            &rec.args_payload,
                        )?
                    };
                    entry.rec_args.push(payload);
                }

                let fmt = analyzer.process_event(
                    &entry.rec_args,
                    &entry.event,
                    entry.event_id,
                    entry.timestamp_ns,
                    entry.record_pid,
                )?;
                fmt_strs.push(fmt);
            }
        }
    }
    Ok(fmt_strs)
}

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
