// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

//======================================================================================================================
// Imports
//======================================================================================================================

use anyhow::Result;
use demikernel::{
    runtime::types::{
        demi_opcode_t,
        demi_qresult_t,
    },
    LibOS,
    QDesc,
    QToken,
};
use std::{
    collections::HashMap,
    net::SocketAddrV4,
};

//======================================================================================================================
// Constants
//======================================================================================================================

#[cfg(target_os = "windows")]
pub const AF_INET: i32 = windows::Win32::Networking::WinSock::AF_INET.0 as i32;

#[cfg(target_os = "windows")]
pub const SOCK_STREAM: i32 = windows::Win32::Networking::WinSock::SOCK_STREAM as i32;

#[cfg(target_os = "linux")]
pub const AF_INET: i32 = libc::AF_INET;

#[cfg(target_os = "linux")]
pub const SOCK_STREAM: i32 = libc::SOCK_STREAM;

//======================================================================================================================
// Structures
//======================================================================================================================

/// TCP Client
pub struct TcpClient {
    /// Underlying libOS.
    libos: LibOS,
    /// Address of remote peer.
    remote: SocketAddrV4,
    /// Number of clients that established a connection.
    clients_connected: usize,
    /// Number of clients that closed their connection.
    clients_closed: usize,
}

//======================================================================================================================
// Associated Functions
//======================================================================================================================

impl TcpClient {
    /// Creates a new TCP client.
    pub fn new(libos: LibOS, remote: SocketAddrV4) -> Result<Self> {
        println!("Connecting to: {:?}", remote);
        Ok(Self {
            libos,
            remote,
            clients_connected: 0,
            clients_closed: 0,
        })
    }

    /// Attempts to close several connections sequentially.
    pub fn run_sequential(&mut self, nclients: usize) -> Result<()> {
        // Open several connections.
        for i in 0..nclients {
            // Create TCP socket.
            let sockqd: QDesc = self.libos.socket(AF_INET, SOCK_STREAM, 0)?;

            // Connect TCP socket.
            let qt: QToken = self.libos.connect(sockqd, self.remote)?;

            // Wait for connection to be established.
            let qr: demi_qresult_t = self.libos.wait(qt, None)?;

            // Parse result.
            match qr.qr_opcode {
                demi_opcode_t::DEMI_OPC_CONNECT => {
                    println!("{} clients connected", i + 1);
                },
                demi_opcode_t::DEMI_OPC_FAILED => panic!("operation failed (qr_ret={:?})", qr.qr_ret),
                qr_opcode => panic!("unexpected result (qr_opcode={:?})", qr_opcode),
            }

            // Close TCP socket.
            self.libos.close(sockqd)?;
        }

        Ok(())
    }

    /// Attempts to close several connections concurrently.
    pub fn run_concurrent(&mut self, nclients: usize) -> Result<()> {
        let mut qds: Vec<QDesc> = Vec::default();
        let mut qts: Vec<QToken> = Vec::default();
        let mut qts_reverse: HashMap<QToken, QDesc> = HashMap::default();

        // Open several connections.
        for _ in 0..nclients {
            // Create TCP socket.
            let qd: QDesc = self.libos.socket(AF_INET, SOCK_STREAM, 0)?;
            qds.push(qd);

            // Connect TCP socket.
            let qt: QToken = self.libos.connect(qd, self.remote)?;
            qts_reverse.insert(qt, qd);
            qts.push(qt);
        }

        // Wait for all connections to be established.
        loop {
            // Stop when enough connections were closed.
            if self.clients_closed >= nclients {
                break;
            }

            let qr: demi_qresult_t = {
                let (index, qr): (usize, demi_qresult_t) = self.libos.wait_any(&qts, None)?;
                let qt: QToken = qts.remove(index);
                qts_reverse
                    .remove(&qt)
                    .ok_or(anyhow::anyhow!("unregistered queue token"))?;
                qr
            };

            // Parse result.
            match qr.qr_opcode {
                demi_opcode_t::DEMI_OPC_CONNECT => {
                    let qd: QDesc = qr.qr_qd.into();

                    self.clients_connected += 1;
                    println!("{} clients connected", self.clients_connected);

                    // Close TCP socket.
                    self.clients_closed += 1;
                    self.libos.close(qd)?;
                },
                demi_opcode_t::DEMI_OPC_FAILED => panic!("operation failed (qr_ret={:?})", qr.qr_ret),
                qr_opcode => panic!("unexpected result (qr_opcode={:?})", qr_opcode),
            }
        }

        Ok(())
    }
}
