// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

//======================================================================================================================
// Imports
//======================================================================================================================

use anyhow::Result;
use clap::{
    Arg,
    ArgMatches,
    Command,
};
use std::{
    net::SocketAddrV4,
    str::FromStr,
};

//======================================================================================================================
// Program Arguments
//======================================================================================================================

/// Program Arguments
#[derive(Debug)]
pub struct ProgramArguments {
    /// Run mode.
    run_mode: String,
    /// Socket IPv4 address.
    addr: SocketAddrV4,
    /// Number of clients
    nclients: Option<usize>,
    /// Peer type.
    peer_type: Option<String>,
}

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
                    .required(false)
                    .value_name("server|client")
                    .help("Sets peer type"),
            )
            .arg(
                Arg::new("nclients")
                    .long("nclients")
                    .value_parser(clap::value_parser!(usize))
                    .required(false)
                    .value_name("NUMBER")
                    .help("Sets number of clients"),
            )
            .arg(
                Arg::new("run-mode")
                    .long("run-mode")
                    .value_parser(clap::value_parser!(String))
                    .required(true)
                    .value_name("standalone|sequential|concurrent")
                    .help("Sets run mode"),
            )
            .get_matches();

        // Run mode.
        let run_mode: String = matches
            .get_one::<String>("run-mode")
            .ok_or(anyhow::anyhow!("missing run mode"))?
            .to_string();

        // Socket address.
        let addr: SocketAddrV4 = {
            let addr: &String = matches.get_one::<String>("addr").expect("missing address");
            SocketAddrV4::from_str(addr)?
        };

        let mut args: ProgramArguments = Self {
            run_mode,
            addr,
            nclients: None,
            peer_type: None,
        };

        // Number of clients.
        if let Some(nclients) = matches.get_one::<usize>("nclients") {
            if *nclients == 0 {
                anyhow::bail!("invalid nclients");
            }
            args.nclients = Some(*nclients);
        }

        // Peer type.
        if let Some(peer_type) = matches.get_one::<String>("peer") {
            if peer_type != "server" && peer_type != "client" {
                anyhow::bail!("invalid peer type");
            }
            args.peer_type = Some(peer_type.to_string());
        }

        Ok(args)
    }

    /// Returns the `addr` command line argument.
    pub fn addr(&self) -> SocketAddrV4 {
        self.addr
    }

    /// Returns the `nclients` command line argument.
    pub fn nclients(&self) -> Option<usize> {
        self.nclients
    }

    /// Returns the `peer_type` command line argument.
    pub fn peer_type(&self) -> Option<String> {
        self.peer_type.clone()
    }

    /// Returns the `run_mode` command line argument.
    pub fn run_mode(&self) -> String {
        self.run_mode.clone()
    }
}
