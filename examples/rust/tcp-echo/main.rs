// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#![cfg_attr(feature = "strict", deny(warnings))]
#![deny(clippy::all)]
#![feature(drain_filter)]
#![feature(hash_drain_filter)]

//======================================================================================================================
// Imports
//======================================================================================================================

use anyhow::Result;
use clap::{
    Arg,
    ArgMatches,
    Command,
};
use client::TcpEchoClient;
use demikernel::{
    LibOS,
    LibOSName,
};
use server::TcpEchoServer;
use std::{
    net::SocketAddrV4,
    str::FromStr,
};

#[cfg(target_os = "windows")]
pub const AF_INET: i32 = windows::Win32::Networking::WinSock::AF_INET.0 as i32;

#[cfg(target_os = "windows")]
pub const SOCK_STREAM: i32 = windows::Win32::Networking::WinSock::SOCK_STREAM as i32;

#[cfg(target_os = "linux")]
pub const AF_INET: i32 = libc::AF_INET;

#[cfg(target_os = "linux")]
pub const SOCK_STREAM: i32 = libc::SOCK_STREAM;

mod client;
mod server;

//======================================================================================================================
// Program Arguments
//======================================================================================================================

/// Program Arguments
#[derive(Debug)]
pub struct ProgramArguments {
    /// Socket IPv4 address.
    saddr: Option<SocketAddrV4>,
    /// Buffer size (in bytes).
    bufsize: Option<usize>,
    /// Number of requests.
    nrequests: Option<usize>,
    /// Log interval.
    log_interval: Option<u64>,
    /// Peer type.
    peer_type: String,
}

/// Associate functions for Program Arguments
impl ProgramArguments {
    /// Parses the program arguments from the command line interface.
    pub fn new(app_name: &'static str, app_author: &'static str, app_about: &'static str) -> Result<Self> {
        let matches: ArgMatches = Command::new(app_name)
            .author(app_author)
            .about(app_about)
            .arg(
                Arg::new("addr")
                    .long("address")
                    .value_parser(clap::value_parser!(String))
                    .required(true)
                    .value_name("ADDRESS:PORT")
                    .help("Sets socket address"),
            )
            .arg(
                Arg::new("peer")
                    .long("peer")
                    .value_parser(clap::value_parser!(String))
                    .required(true)
                    .value_name("server|client")
                    .default_value("server")
                    .help("Sets peer type"),
            )
            .arg(
                Arg::new("bufsize")
                    .long("bufsize")
                    .value_parser(clap::value_parser!(usize))
                    .required(false)
                    .value_name("SIZE")
                    .help("Sets buffer size"),
            )
            .arg(
                Arg::new("nrequests")
                    .long("nrequests")
                    .value_parser(clap::value_parser!(usize))
                    .required(false)
                    .value_name("NUMBER")
                    .help("Sets number of requests"),
            )
            .arg(
                Arg::new("log")
                    .long("log")
                    .value_parser(clap::value_parser!(u64))
                    .required(false)
                    .value_name("INTERVAL")
                    .help("Enables logging"),
            )
            .get_matches();

        // Default arguments.
        let mut args: ProgramArguments = ProgramArguments {
            saddr: None,
            bufsize: None,
            nrequests: None,
            log_interval: None,
            peer_type: "server".to_string(),
        };

        // Socket address.
        if let Some(addr) = matches.get_one::<String>("addr") {
            let ref mut this = args;
            this.saddr = Some(SocketAddrV4::from_str(addr)?);
        }

        // Buffer size.
        if let Some(bufsize) = matches.get_one::<usize>("bufsize") {
            if *bufsize > 0 {
                args.bufsize = Some(*bufsize);
            } else {
                anyhow::bail!("invalid buffer size");
            }
        }

        // Number of requests.
        if let Some(nrequests) = matches.get_one::<usize>("nrequests") {
            if *nrequests > 0 {
                args.nrequests = Some(*nrequests);
            } else {
                anyhow::bail!("invalid buffer size");
            }
        }

        // Log interval.
        if let Some(log_interval) = matches.get_one::<u64>("log") {
            if *log_interval > 0 {
                args.log_interval = Some(*log_interval);
            } else {
                anyhow::bail!("invalid log_interval");
            }
        }

        // Peer type
        if let Some(peer_type) = matches.get_one::<String>("peer") {
            let ref mut this = args;
            let peer_type = peer_type.to_string();
            if peer_type != "server" && peer_type != "client" {
                anyhow::bail!("invalid peer type");
            } else {
                this.peer_type = peer_type;
            }
        }

        Ok(args)
    }
}

//======================================================================================================================

fn main() -> Result<()> {
    let args: ProgramArguments = ProgramArguments::new(
        "tcp-echo",
        "Pedro Henrique Penna <ppenna@microsoft.com>",
        "Echoes TCP packets.",
    )?;

    let libos_name: LibOSName = match LibOSName::from_env() {
        Ok(libos_name) => libos_name.into(),
        Err(e) => panic!("{:?}", e),
    };
    let libos: LibOS = match LibOS::new(libos_name) {
        Ok(libos) => libos,
        Err(e) => panic!("failed to initialize libos: {:?}", e.cause),
    };

    match args.peer_type.as_str() {
        "server" => {
            let mut server: TcpEchoServer = TcpEchoServer::new(libos, args.saddr.unwrap())?;
            server.run(args.log_interval, args.nrequests)?;
        },
        "client" => {
            let mut client: TcpEchoClient = TcpEchoClient::new(
                libos,
                args.bufsize.ok_or(anyhow::anyhow!("missing buffer size"))?,
                args.saddr.unwrap(),
            )?;
            client.run(args.log_interval, args.nrequests)?;
        },
        _ => todo!(),
    }

    Ok(())
}
