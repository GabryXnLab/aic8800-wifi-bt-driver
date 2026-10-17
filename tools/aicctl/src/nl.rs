//! Netlink generico minimo: solo quanto serve a nl80211 (risoluzione della famiglia, richieste
//! con risposta, ascolto di un gruppo multicast). A mano con `libc` per non portare un client
//! netlink intero per due messaggi.

use std::io;
use std::os::fd::{AsRawFd, FromRawFd, OwnedFd};
use std::time::Duration;

const NETLINK_GENERIC: i32 = 16;
const SOL_NETLINK: i32 = 270;
const NETLINK_ADD_MEMBERSHIP: i32 = 1;
const GENL_ID_CTRL: u16 = 0x10;
const CTRL_CMD_GETFAMILY: u8 = 3;
const CTRL_ATTR_FAMILY_ID: u16 = 1;
const CTRL_ATTR_FAMILY_NAME: u16 = 2;
const CTRL_ATTR_MCAST_GROUPS: u16 = 7;
const CTRL_ATTR_MCAST_GRP_NAME: u16 = 1;
const CTRL_ATTR_MCAST_GRP_ID: u16 = 2;
const NLMSG_ERROR: u16 = 2;
const NLMSG_DONE: u16 = 3;
const NLM_F_REQUEST: u16 = 1;
const NLM_F_ACK: u16 = 4;
const NLA_TYPE_MASK: u16 = 0x3fff;

pub struct Attr {
    pub ty: u16,
    pub data: Vec<u8>,
}

pub enum Frame {
    Ack,
    Err(i32),
    Done,
    Genl { seq: u32, cmd: u8, attrs: Vec<Attr> },
}

pub struct Family {
    pub id: u16,
    pub groups: Vec<(String, u32)>,
}

fn align4(n: usize) -> usize {
    (n + 3) & !3
}

pub fn put_attr(buf: &mut Vec<u8>, ty: u16, data: &[u8]) {
    buf.extend_from_slice(&((4 + data.len()) as u16).to_ne_bytes());
    buf.extend_from_slice(&ty.to_ne_bytes());
    buf.extend_from_slice(data);
    buf.resize(align4(buf.len()), 0);
}

pub fn parse_attrs(mut buf: &[u8]) -> Vec<Attr> {
    let mut out = Vec::new();
    while buf.len() >= 4 {
        let len = u16::from_ne_bytes([buf[0], buf[1]]) as usize;
        let ty = u16::from_ne_bytes([buf[2], buf[3]]) & NLA_TYPE_MASK;
        if len < 4 || len > buf.len() {
            break;
        }
        out.push(Attr { ty, data: buf[4..len].to_vec() });
        buf = &buf[align4(len).min(buf.len())..];
    }
    out
}

pub fn parse_datagram(mut buf: &[u8]) -> Vec<Frame> {
    let mut out = Vec::new();
    while buf.len() >= 16 {
        let len = u32::from_ne_bytes(buf[0..4].try_into().unwrap()) as usize;
        let ty = u16::from_ne_bytes([buf[4], buf[5]]);
        let seq = u32::from_ne_bytes(buf[8..12].try_into().unwrap());
        if len < 16 || len > buf.len() {
            break;
        }
        let payload = &buf[16..len];
        match ty {
            NLMSG_ERROR if payload.len() >= 4 => {
                let e = i32::from_ne_bytes(payload[0..4].try_into().unwrap());
                out.push(if e == 0 { Frame::Ack } else { Frame::Err(-e) });
            }
            NLMSG_DONE => out.push(Frame::Done),
            _ if payload.len() >= 4 => out.push(Frame::Genl {
                seq,
                cmd: payload[0],
                attrs: parse_attrs(&payload[4..]),
            }),
            _ => {}
        }
        buf = &buf[align4(len).min(buf.len())..];
    }
    out
}

pub struct Genl {
    fd: OwnedFd,
    seq: u32,
}

impl Genl {
    pub fn open(timeout: Option<Duration>) -> io::Result<Self> {
        let raw = unsafe { libc::socket(libc::AF_NETLINK, libc::SOCK_RAW | libc::SOCK_CLOEXEC, NETLINK_GENERIC) };
        if raw < 0 {
            return Err(io::Error::last_os_error());
        }
        let fd = unsafe { OwnedFd::from_raw_fd(raw) };
        if let Some(t) = timeout {
            let tv = libc::timeval { tv_sec: t.as_secs() as _, tv_usec: t.subsec_micros() as _ };
            let r = unsafe {
                libc::setsockopt(
                    fd.as_raw_fd(),
                    libc::SOL_SOCKET,
                    libc::SO_RCVTIMEO,
                    &tv as *const _ as *const libc::c_void,
                    std::mem::size_of::<libc::timeval>() as u32,
                )
            };
            if r < 0 {
                return Err(io::Error::last_os_error());
            }
        }
        Ok(Genl { fd, seq: 1 })
    }

    pub fn join(&self, group: u32) -> io::Result<()> {
        let r = unsafe {
            libc::setsockopt(
                self.fd.as_raw_fd(),
                SOL_NETLINK,
                NETLINK_ADD_MEMBERSHIP,
                &group as *const _ as *const libc::c_void,
                4,
            )
        };
        if r < 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(())
    }

    fn send(&mut self, ty: u16, cmd: u8, attrs: &[u8]) -> io::Result<u32> {
        self.seq += 1;
        let mut m = Vec::with_capacity(20 + attrs.len());
        m.extend_from_slice(&((20 + attrs.len()) as u32).to_ne_bytes());
        m.extend_from_slice(&ty.to_ne_bytes());
        m.extend_from_slice(&(NLM_F_REQUEST | NLM_F_ACK).to_ne_bytes());
        m.extend_from_slice(&self.seq.to_ne_bytes());
        m.extend_from_slice(&0u32.to_ne_bytes());
        m.extend_from_slice(&[cmd, 1, 0, 0]);
        m.extend_from_slice(attrs);
        let n = unsafe { libc::send(self.fd.as_raw_fd(), m.as_ptr() as *const _, m.len(), 0) };
        if n < 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(self.seq)
    }

    pub fn recv(&self) -> io::Result<Vec<Frame>> {
        let mut buf = vec![0u8; 65536];
        let n = unsafe { libc::recv(self.fd.as_raw_fd(), buf.as_mut_ptr() as *mut _, buf.len(), 0) };
        if n < 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(parse_datagram(&buf[..n as usize]))
    }

    /// Invia un comando e raccoglie gli attributi dei messaggi di risposta fino all'ACK.
    pub fn request(&mut self, ty: u16, cmd: u8, attrs: &[u8]) -> io::Result<Vec<Vec<Attr>>> {
        let seq = self.send(ty, cmd, attrs)?;
        let mut replies = Vec::new();
        loop {
            for f in self.recv()? {
                match f {
                    Frame::Err(e) => return Err(io::Error::from_raw_os_error(e)),
                    Frame::Ack | Frame::Done => return Ok(replies),
                    Frame::Genl { seq: s, attrs, .. } if s == seq => replies.push(attrs),
                    Frame::Genl { .. } => {}
                }
            }
        }
    }

    pub fn family(&mut self, name: &str) -> io::Result<Family> {
        let mut a = Vec::new();
        let mut n = name.as_bytes().to_vec();
        n.push(0);
        put_attr(&mut a, CTRL_ATTR_FAMILY_NAME, &n);
        let replies = self.request(GENL_ID_CTRL, CTRL_CMD_GETFAMILY, &a)?;
        let attrs = replies.into_iter().next().ok_or_else(|| io::Error::from_raw_os_error(libc::ENOENT))?;
        let mut fam = Family { id: 0, groups: Vec::new() };
        for at in &attrs {
            match at.ty {
                CTRL_ATTR_FAMILY_ID if at.data.len() >= 2 => fam.id = u16::from_ne_bytes([at.data[0], at.data[1]]),
                CTRL_ATTR_MCAST_GROUPS => {
                    for g in parse_attrs(&at.data) {
                        let mut name = String::new();
                        let mut id = 0;
                        for f in parse_attrs(&g.data) {
                            match f.ty {
                                CTRL_ATTR_MCAST_GRP_NAME => {
                                    name = String::from_utf8_lossy(&f.data).trim_end_matches('\0').to_string()
                                }
                                CTRL_ATTR_MCAST_GRP_ID if f.data.len() >= 4 => {
                                    id = u32::from_ne_bytes(f.data[0..4].try_into().unwrap())
                                }
                                _ => {}
                            }
                        }
                        fam.groups.push((name, id));
                    }
                }
                _ => {}
            }
        }
        Ok(fam)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn attrs_roundtrip_con_padding() {
        let mut b = Vec::new();
        put_attr(&mut b, 3, &[1]);
        put_attr(&mut b, 6, &10000u32.to_ne_bytes());
        assert_eq!(b.len(), 16);
        let a = parse_attrs(&b);
        assert_eq!(a.len(), 2);
        assert_eq!((a[0].ty, a[0].data.as_slice()), (3, &[1u8][..]));
        assert_eq!(u32::from_ne_bytes(a[1].data[..4].try_into().unwrap()), 10000);
    }

    #[test]
    fn attrs_troncati_non_vanno_in_panico() {
        assert!(parse_attrs(&[9, 0, 1, 0, 0]).is_empty());
    }

    #[test]
    fn datagram_con_errore() {
        let mut m = Vec::new();
        m.extend_from_slice(&20u32.to_ne_bytes());
        m.extend_from_slice(&NLMSG_ERROR.to_ne_bytes());
        m.extend_from_slice(&[0; 10]);
        m.extend_from_slice(&(-1i32).to_ne_bytes());
        assert!(matches!(parse_datagram(&m)[0], Frame::Err(1)));
    }
}
