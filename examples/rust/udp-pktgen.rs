// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#![cfg_attr(feature = "strict", deny(warnings))]
#![deny(clippy::all)]

//==============================================================================
// Imports
//==============================================================================

use ::anyhow::{
    bail,
    Result,
};
use ::clap::{
    Arg,
    ArgMatches,
    Command,
};
use ::demikernel::{
    demi_sgarray_t,
    runtime::types::demi_opcode_t,
    LibOS,
    LibOSName,
    QDesc,
    QToken,
};
use ::std::{
    net::SocketAddrV4,
    slice,
    str::FromStr,
    time::{
        Duration,
        Instant,
    },
};

//==============================================================================
// Program Arguments
//==============================================================================

/// Program Arguments
#[derive(Debug)]
pub struct ProgramArguments {
    /// Local socket IPv4 address.
    local: SocketAddrV4,
    /// Remote socket IPv4 address.
    remote: SocketAddrV4,
    /// Buffer size (in bytes).
    bufsize: usize,
    /// Injection rate (in micro-seconds).
    injection_rate: u64,
}

/// Associate functions for Program Arguments
impl ProgramArguments {
    // Default buffer size.
    const DEFAULT_BUFSIZE: usize = 1024;
    // Default injection rate.
    const DEFAULT_INJECTION_RATE: u64 = 100;
    /// Default local address.
    const DEFAULT_LOCAL: &'static str = "127.0.0.1:12345";
    /// Default host address.
    const DEFAULT_REMOTE: &'static str = "127.0.0.1:23456";

    /// Parses the program arguments from the command line interface.
    pub fn new(app_name: &'static str, app_author: &'static str, app_about: &'static str) -> Result<Self> {
        let matches: ArgMatches = Command::new(app_name)
            .author(app_author)
            .about(app_about)
            .arg(
                Arg::new("local")
                    .long("local")
                    .value_parser(clap::value_parser!(String))
                    .required(false)
                    .value_name("ADDRESS:PORT")
                    .help("Sets local address"),
            )
            .arg(
                Arg::new("remote")
                    .long("remote")
                    .value_parser(clap::value_parser!(String))
                    .required(true)
                    .value_name("ADDRESS:PORT")
                    .help("Sets remote address"),
            )
            .arg(
                Arg::new("bufsize")
                    .long("bufsize")
                    .value_parser(clap::value_parser!(String))
                    .required(true)
                    .value_name("SIZE")
                    .help("Sets buffer size"),
            )
            .arg(
                Arg::new("injection_rate")
                    .long("injection_rate")
                    .value_parser(clap::value_parser!(String))
                    .required(true)
                    .value_name("RATE")
                    .help("Sets packet injection rate"),
            )
            .get_matches();

        // Default arguments.
        let mut args: ProgramArguments = ProgramArguments {
            local: SocketAddrV4::from_str(Self::DEFAULT_LOCAL)?,
            remote: SocketAddrV4::from_str(Self::DEFAULT_REMOTE)?,
            bufsize: Self::DEFAULT_BUFSIZE,
            injection_rate: Self::DEFAULT_INJECTION_RATE,
        };

        // Local address.
        if let Some(addr) = matches.get_one::<String>("local") {
            args.set_local_addr(addr)?;
        }

        // Remote address.
        if let Some(addr) = matches.get_one::<String>("remote") {
            args.set_remote_addr(addr)?;
        }

        // Buffer size.
        if let Some(bufsize) = matches.get_one::<String>("bufsize") {
            args.set_bufsize(bufsize)?;
        }

        // Injection rate.
        if let Some(injection_rate) = matches.get_one::<String>("injection_rate") {
            args.set_injection_rate(injection_rate)?;
        }

        Ok(args)
    }

    /// Returns the local endpoint address parameter stored in the target program arguments.
    pub fn get_local(&self) -> SocketAddrV4 {
        self.local
    }

    /// Returns the remote endpoint address parameter stored in the target program arguments.
    pub fn get_remote(&self) -> SocketAddrV4 {
        self.remote
    }

    /// Returns the buffer size parameter stored in the target program arguments.
    pub fn get_bufsize(&self) -> usize {
        self.bufsize
    }

    /// Returns the injection rate parameter stored in the target program arguments.
    pub fn get_injection_rate(&self) -> u64 {
        self.injection_rate
    }

    /// Sets the local address and port number parameters in the target program arguments.
    fn set_local_addr(&mut self, addr: &str) -> Result<()> {
        self.local = SocketAddrV4::from_str(addr)?;
        Ok(())
    }

    /// Sets the remote address and port number parameters in the target program arguments.
    fn set_remote_addr(&mut self, addr: &str) -> Result<()> {
        self.remote = SocketAddrV4::from_str(addr)?;
        Ok(())
    }

    /// Sets the buffer size parameter in the target program arguments.
    fn set_bufsize(&mut self, bufsize_str: &str) -> Result<()> {
        let bufsize: usize = bufsize_str.parse()?;
        if bufsize > 0 {
            self.bufsize = bufsize;
            Ok(())
        } else {
            bail!("invalid buffer size")
        }
    }

    /// Sets the injection rate parameter in the target program arguments.
    fn set_injection_rate(&mut self, injection_rate_str: &str) -> Result<()> {
        let injection_rate: u64 = injection_rate_str.parse()?;
        if injection_rate > 0 {
            self.injection_rate = injection_rate;
            Ok(())
        } else {
            bail!("invalid injection rate")
        }
    }
}

//==============================================================================
// Application
//==============================================================================

/// Application
struct Application {
    /// Underlying libOS.
    libos: LibOS,
    // Local socket descriptor.
    sockqd: QDesc,
    /// Remote endpoint.
    remote: SocketAddrV4,
    /// Buffer size.
    bufsize: usize,
    /// Injection rate
    injection_rate: u64,
}

/// Associated Functions for the Application
impl Application {
    /// Logging interval (in seconds).
    const LOG_INTERVAL: u64 = 5;

    /// Instantiates the application.
    pub fn new(mut libos: LibOS, args: &ProgramArguments) -> Self {
        // Extract arguments.
        let local: SocketAddrV4 = args.get_local();
        let remote: SocketAddrV4 = args.get_remote();
        let bufsize: usize = args.get_bufsize();
        let injection_rate: u64 = args.get_injection_rate();

        // Create UDP socket.
        let sockqd: QDesc = match libos.socket(libc::AF_INET, libc::SOCK_DGRAM, 1) {
            Ok(qd) => qd,
            Err(e) => panic!("failed to create socket: {:?}", e.cause),
        };

        // Bind to local address.
        match libos.bind(sockqd, local) {
            Ok(()) => (),
            Err(e) => panic!("failed to bind socket: {:?}", e.cause),
        };

        println!("Local Address:  {:?}", local);
        println!("Remote Address: {:?}", remote);

        Self {
            libos,
            sockqd,
            remote,
            bufsize,
            injection_rate,
        }
    }

    /// Runs the target application.
    pub fn run(&mut self) {
        let mut nbytes: usize = 0;
        let start: Instant = Instant::now();
        let mut last_push: Instant = Instant::now();
        let mut last_log: Instant = Instant::now();

        loop {
            // Dump statistics.
            if last_log.elapsed() > Duration::from_secs(Self::LOG_INTERVAL) {
                let elapsed: Duration = Instant::now() - start;
                println!("{:?} B / {:?} us", nbytes, elapsed.as_micros());
                last_log = Instant::now();
            }

            // Push packet.
            if last_push.elapsed() > Duration::from_nanos(self.injection_rate) {
                let sga: demi_sgarray_t = self.mksga(self.bufsize, 0x65);

                let qt: QToken = match self.libos.pushto(self.sockqd, &sga, self.remote) {
                    Ok(qt) => qt,
                    Err(e) => panic!("failed to push data to socket: {:?}", e.cause),
                };
                match self.libos.wait(qt) {
                    Ok(qr) if qr.qr_opcode == demi_opcode_t::DEMI_OPC_PUSH => (),
                    Err(e) => panic!("operation failed: {:?}", e.cause),
                    _ => panic!("unexpected result"),
                };
                nbytes += sga.sga_segs[0].sgaseg_len as usize;
                match self.libos.sgafree(sga) {
                    Ok(_) => {},
                    Err(e) => panic!("failed to release scatter-gather array: {:?}", e),
                }

                last_push = Instant::now();
            }
        }
    }

    // Makes a scatter-gather array.
    fn mksga(&mut self, size: usize, value: u8) -> demi_sgarray_t {
        // Allocate scatter-gather array.
        let sga: demi_sgarray_t = match self.libos.sgaalloc(size) {
            Ok(sga) => sga,
            Err(e) => panic!("failed to allocate scatter-gather array: {:?}", e),
        };

        // Ensure that scatter-gather array has the requested size.
        assert!(sga.sga_segs[0].sgaseg_len as usize == size);

        // Fill in scatter-gather array.
        let ptr: *mut u8 = sga.sga_segs[0].sgaseg_buf as *mut u8;
        let len: usize = sga.sga_segs[0].sgaseg_len as usize;
        let slice: &mut [u8] = unsafe { slice::from_raw_parts_mut(ptr, len) };
        slice.fill(value);

        sga
    }
}

//==============================================================================

/// Drives the application.
fn main() -> Result<()> {
    let args: ProgramArguments = ProgramArguments::new(
        "udp-pktgen",
        "Pedro Henrique Penna <ppenna@microsoft.com>",
        "Generates UDP traffic.",
    )?;

    let libos_name: LibOSName = match LibOSName::from_env() {
        Ok(libos_name) => libos_name.into(),
        Err(e) => panic!("{:?}", e),
    };
    let libos: LibOS = match LibOS::new(libos_name) {
        Ok(libos) => libos,
        Err(e) => panic!("failed to initialize libos: {:?}", e.cause),
    };

    Application::new(libos, &args).run();

    Ok(())
}
