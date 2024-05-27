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

use clap::Arg;
use clap::Command;
use thiserror::Error;
use trace::Event;

#[derive(Error, Debug)]
pub enum Error
{
    #[error("usage: {0} [--no-header] <trace-events> <trace-file>")]
    CliOptionUnmatch(String),
}

pub type Result<T> = std::result::Result<T, Error>;

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
