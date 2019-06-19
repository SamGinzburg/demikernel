use crate::{
    protocols::{arp, ethernet2::MacAddress},
    rand::Seed,
};
use base64::{decode_config_slice, STANDARD_NO_PAD};
use std::net::Ipv4Addr;

#[derive(Clone)]
pub struct Options {
    pub my_link_addr: MacAddress,
    pub my_ipv4_addr: Ipv4Addr,
    pub arp: arp::Options,
    pub rng_seed: Option<String>,
}

impl Options {
    pub fn decode_rng_seed(&self) -> Seed {
        let mut seed = Seed::default();
        if let Some(s) = self.rng_seed.as_ref() {
            decode_config_slice(&s, STANDARD_NO_PAD, seed.as_mut()).unwrap();
        }

        seed
    }
}
