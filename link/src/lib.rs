//! The link between paired Needfuls: an encrypted connection addressed by
//! each install's key, found on the same network by mDNS or dialed at an
//! address, and a one-time code for pairing two installs.
//!
//! It knows nothing about lists or files. Needful (C++) drives it through a
//! small C interface: commands go in as JSON, events come out as JSON, and a
//! pipe becomes readable whenever there are events to take.
//!
//! No outside server is used. iroh's public relays and DNS address lookup are
//! never turned on; a relay is used only when one is named.

use std::collections::{HashMap, HashSet, VecDeque};
use std::ffi::{c_char, c_int, CStr, CString};
use std::net::{IpAddr, SocketAddr};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use base64::Engine as _;
use hmac::{Hmac, Mac};
use iroh::address_lookup::UserData;
use iroh::endpoint::{presets, Connection, RecvStream, SendStream};
use iroh::{Endpoint, EndpointAddr, EndpointId, RelayMode, RelayUrl, SecretKey, TransportAddr};
use iroh_mdns_address_lookup::{DiscoveryEvent, MdnsAddressLookup};
use n0_future::StreamExt;
use serde_json::{json, Value};
use sha2::Sha256;
use spake2::{Ed25519Group, Identity, Password, Spake2};
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::{TcpListener, TcpStream};
use tokio::sync::{mpsc, oneshot};

const LINK_ALPN: &[u8] = b"needful/link/1";
const PAIR_ALPN: &[u8] = b"needful/pair/1";
const MDNS_SERVICE: &str = "needful";
const MAX_FRAME: usize = 64 << 20;
const B64: base64::engine::GeneralPurpose = base64::engine::general_purpose::STANDARD;

pub fn id_hex(id: &EndpointId) -> String {
    hex::encode(id.as_bytes())
}

pub fn parse_id(text: &str) -> Option<EndpointId> {
    let bytes: [u8; 32] = hex::decode(text).ok()?.try_into().ok()?;
    EndpointId::from_bytes(&bytes).ok()
}

/// How a connection reaches the other machine right now.
fn path_of(conn: &Connection) -> (String, Vec<String>) {
    let mut kind = String::from("direct");
    let mut addrs = Vec::new();
    for path in conn.paths().iter() {
        if !path.is_selected() {
            continue;
        }
        match path.remote_addr() {
            TransportAddr::Relay(_) => kind = "relay".into(),
            TransportAddr::Ip(addr) => {
                kind = if is_local(&addr.ip()) { "local".into() } else { "direct".into() };
                addrs.push(addr.to_string());
            }
            _ => {}
        }
    }
    (kind, addrs)
}

fn is_local(ip: &IpAddr) -> bool {
    match ip {
        IpAddr::V4(v4) => v4.is_private() || v4.is_loopback() || v4.is_link_local(),
        IpAddr::V6(v6) => {
            v6.is_loopback() || (v6.segments()[0] & 0xfe00) == 0xfc00 || (v6.segments()[0] & 0xffc0) == 0xfe80
        }
    }
}

struct Peer {
    tx: mpsc::UnboundedSender<Vec<u8>>,
    conn: Connection,
    initiator: EndpointId,
}

struct Pairing {
    code: String,
    info: Value,
    busy: bool,
}

struct Shared {
    me: EndpointId,
    events: Mutex<VecDeque<String>>,
    wake: c_int,
    trusted: Mutex<HashSet<EndpointId>>,
    peers: Mutex<HashMap<EndpointId, Peer>>,
    pairing: Mutex<Option<Pairing>>,
    answer: Mutex<Option<oneshot::Sender<bool>>>,
    seen: Mutex<HashMap<EndpointId, Option<String>>>,
    relay: Option<RelayUrl>,
    default_port: u16,
}

impl Shared {
    fn emit(&self, event: Value) {
        self.events.lock().unwrap().push_back(event.to_string());
        let byte = [1u8];
        // SAFETY: wake is the write end of a pipe this link owns.
        unsafe { libc::write(self.wake, byte.as_ptr().cast(), 1) };
    }
}


pub struct Link {
    runtime: tokio::runtime::Runtime,
    shared: Arc<Shared>,
    endpoint: Endpoint,
    read_fd: c_int,
}

#[derive(serde::Deserialize)]
struct Config {
    secret: String,
    #[serde(default)]
    port: u16,
    #[serde(default)]
    relay: Option<String>,
    #[serde(default = "yes")]
    mdns: bool,
    #[serde(default)]
    loopback: bool,
}

fn yes() -> bool {
    true
}

async fn read_frame(recv: &mut RecvStream) -> anyhow::Result<Vec<u8>> {
    let mut len = [0u8; 4];
    recv.read_exact(&mut len).await?;
    let len = u32::from_be_bytes(len) as usize;
    anyhow::ensure!(len <= MAX_FRAME, "frame too large");
    let mut buf = vec![0u8; len];
    recv.read_exact(&mut buf).await?;
    Ok(buf)
}

async fn write_frame(send: &mut SendStream, data: &[u8]) -> anyhow::Result<()> {
    send.write_all(&(data.len() as u32).to_be_bytes()).await?;
    send.write_all(data).await?;
    Ok(())
}

async fn read_json(recv: &mut RecvStream) -> anyhow::Result<Value> {
    Ok(serde_json::from_slice(&read_frame(recv).await?)?)
}

async fn write_json(send: &mut SendStream, value: &Value) -> anyhow::Result<()> {
    write_frame(send, value.to_string().as_bytes()).await
}

fn confirmation(key: &[u8], role: &str, joiner: &EndpointId, pairer: &EndpointId) -> Vec<u8> {
    let mut mac = <Hmac<Sha256> as Mac>::new_from_slice(key).expect("any key length");
    mac.update(b"needful-pair-v1 ");
    mac.update(role.as_bytes());
    mac.update(joiner.as_bytes());
    mac.update(pairer.as_bytes());
    mac.finalize().into_bytes().to_vec()
}

fn b64(data: &[u8]) -> Value {
    Value::String(B64.encode(data))
}

fn unb64(value: &Value) -> anyhow::Result<Vec<u8>> {
    Ok(B64.decode(value.as_str().ok_or_else(|| anyhow::anyhow!("not text"))?)?)
}

/// The nameplate is the code's leading number, which tells a joiner on the
/// same network which machine is waiting, without giving away the rest.
fn nameplate(code: &str) -> String {
    code.split('-').next().unwrap_or("").to_string()
}

fn normalize(code: &str) -> String {
    code.trim()
        .to_lowercase()
        .split(|c: char| c == '-' || c.is_whitespace())
        .filter(|s| !s.is_empty())
        .collect::<Vec<_>>()
        .join("-")
}

impl Link {
    fn start(config: Config) -> anyhow::Result<Link> {
        let runtime = tokio::runtime::Builder::new_multi_thread()
            .worker_threads(2)
            .enable_all()
            .thread_name("needful-link")
            .build()?;
        let secret: [u8; 32] = hex::decode(&config.secret)?
            .try_into()
            .map_err(|_| anyhow::anyhow!("the secret key must be 32 bytes"))?;
        let secret = SecretKey::from_bytes(&secret);
        let relay = match config.relay.as_deref() {
            Some(url) if !url.is_empty() => Some(url.parse::<RelayUrl>()?),
            _ => None,
        };

        let mut fds = [0 as c_int; 2];
        // SAFETY: fds has room for two descriptors.
        if unsafe { libc::pipe(fds.as_mut_ptr()) } != 0 {
            anyhow::bail!("could not make a pipe");
        }
        unsafe {
            libc::fcntl(fds[0], libc::F_SETFL, libc::O_NONBLOCK);
            libc::fcntl(fds[1], libc::F_SETFL, libc::O_NONBLOCK);
        }

        let endpoint = runtime.block_on(async {
            let bind = |port: u16| {
                let mut builder = Endpoint::builder(presets::Minimal)
                    .secret_key(secret.clone())
                    .alpns(vec![LINK_ALPN.to_vec(), PAIR_ALPN.to_vec()])
                    .relay_mode(match &relay {
                        Some(url) => RelayMode::custom([url.clone()]),
                        None => RelayMode::Disabled,
                    })
                    .clear_ip_transports();
                let host4 = if config.loopback { "127.0.0.1" } else { "0.0.0.0" };
                builder = builder.bind_addr(format!("{host4}:{port}").as_str())?;
                if !config.loopback {
                    builder = builder.bind_addr(format!("[::]:{port}").as_str())?;
                }
                anyhow::Ok(builder)
            };
            let endpoint = match bind(config.port)?.bind().await {
                Ok(endpoint) => endpoint,
                // Something else has the usual port; the network still finds
                // this machine by mDNS, and the status says which port it got.
                Err(_) if config.port != 0 => bind(0)?.bind().await?,
                Err(error) => return Err(error.into()),
            };
            anyhow::Ok(endpoint)
        })?;

        let shared = Arc::new(Shared {
            me: endpoint.id(),
            events: Mutex::new(VecDeque::new()),
            wake: fds[1],
            trusted: Mutex::new(HashSet::new()),
            peers: Mutex::new(HashMap::new()),
            pairing: Mutex::new(None),
            answer: Mutex::new(None),
            seen: Mutex::new(HashMap::new()),
            relay,
            default_port: if config.port == 0 { 7787 } else { config.port },
        });

        let port = endpoint
            .bound_sockets()
            .iter()
            .find(|a| a.is_ipv4())
            .map(|a| a.port())
            .unwrap_or(0);

        let link = Link { runtime, shared: shared.clone(), endpoint: endpoint.clone(), read_fd: fds[0] };

        // The plain TCP port beside the link's UDP one only says which key
        // is waiting to pair, so `join --at host` can find it. Pairing then
        // proves both sides know the code; nothing else is said here.
        let tcp = link.runtime.block_on(async {
            let host = if config.loopback { "127.0.0.1" } else { "0.0.0.0" };
            TcpListener::bind((host, port)).await.ok()
        });
        if let Some(listener) = tcp {
            let shared = shared.clone();
            link.runtime.spawn(async move {
                while let Ok((mut socket, _)) = listener.accept().await {
                    let open = shared.pairing.lock().unwrap().is_some();
                    let line = if open {
                        json!({"needful": 1, "id": id_hex(&shared.me), "pairing": true})
                    } else {
                        json!({"needful": 1, "pairing": false})
                    };
                    let _ = socket.write_all(format!("{line}\n").as_bytes()).await;
                }
            });
        }

        if config.mdns {
            let ep = endpoint.clone();
            let shared2 = shared.clone();
            link.runtime.spawn(async move {
                let Ok(mdns) = MdnsAddressLookup::builder().service_name(MDNS_SERVICE).build(ep.id()) else {
                    shared2.emit(json!({"ev": "note", "text": "local discovery is unavailable on this network"}));
                    return;
                };
                if let Ok(lookup) = ep.address_lookup() {
                    lookup.add(mdns.clone());
                }
                let mut events = mdns.subscribe().await;
                while let Some(event) = events.next().await {
                    match event {
                        DiscoveryEvent::Discovered { endpoint_info, .. } => {
                            let id = endpoint_info.endpoint_id;
                            let data = endpoint_info.data.user_data().map(|u| u.to_string());
                            let fresh = shared2.seen.lock().unwrap().insert(id, data.clone()) != Some(data.clone());
                            if fresh {
                                shared2.emit(json!({"ev": "seen", "id": id_hex(&id)}));
                            }
                        }
                        DiscoveryEvent::Expired { endpoint_id } => {
                            shared2.seen.lock().unwrap().remove(&endpoint_id);
                        }
                        _ => {}
                    }
                }
            });
        }

        let ep = endpoint.clone();
        let shared2 = shared.clone();
        link.runtime.spawn(async move {
            while let Some(incoming) = ep.accept().await {
                let shared = shared2.clone();
                let ep = ep.clone();
                tokio::spawn(async move {
                    let Ok(accepting) = incoming.accept() else { return };
                    let Ok(conn) = accepting.await else { return };
                    let id = conn.remote_id();
                    if conn.alpn() == LINK_ALPN {
                        if shared.trusted.lock().unwrap().contains(&id) {
                            run_peer(shared, conn, false).await;
                        } else {
                            conn.close(1u32.into(), b"not paired");
                        }
                    } else if conn.alpn() == PAIR_ALPN {
                        let _ = pair_as_host(shared, ep, conn).await;
                    } else {
                        conn.close(1u32.into(), b"unknown");
                    }
                });
            }
        });

        shared.emit(json!({"ev": "ready", "id": id_hex(&shared.me), "port": port}));
        Ok(link)
    }

    fn command(&self, text: &str) {
        let Ok(cmd) = serde_json::from_str::<Value>(text) else { return };
        let shared = self.shared.clone();
        let ep = self.endpoint.clone();
        let op = cmd["op"].as_str().unwrap_or("").to_string();
        match op.as_str() {
            "trust" => {
                let ids: HashSet<EndpointId> =
                    cmd["ids"].as_array().into_iter().flatten().filter_map(|v| parse_id(v.as_str()?)).collect();
                *shared.trusted.lock().unwrap() = ids.clone();
                // Anyone no longer trusted is cut off at once.
                let gone: Vec<Connection> = shared
                    .peers
                    .lock()
                    .unwrap()
                    .iter()
                    .filter(|(id, _)| !ids.contains(*id))
                    .map(|(_, p)| p.conn.clone())
                    .collect();
                for conn in gone {
                    conn.close(2u32.into(), b"unpaired");
                }
            }
            "dial" => {
                let Some(id) = cmd["id"].as_str().and_then(parse_id) else { return };
                let hosts: Vec<String> =
                    cmd["addrs"].as_array().into_iter().flatten().filter_map(|v| v.as_str().map(String::from)).collect();
                self.runtime.spawn(async move {
                    if shared.peers.lock().unwrap().contains_key(&id) {
                        return;
                    }
                    let mut addrs = Vec::new();
                    for host in hosts {
                        addrs.extend(resolve(&host, shared.default_port).await.into_iter().map(TransportAddr::Ip));
                    }
                    if let Some(relay) = &shared.relay {
                        addrs.push(TransportAddr::Relay(relay.clone()));
                    }
                    let addr = EndpointAddr::from_parts(id, addrs);
                    match tokio::time::timeout(Duration::from_secs(15), ep.connect(addr, LINK_ALPN)).await {
                        Ok(Ok(conn)) => run_peer(shared, conn, true).await,
                        _ => shared.emit(json!({"ev": "unreachable", "id": id_hex(&id)})),
                    }
                });
            }
            "send" => {
                let Some(id) = cmd["id"].as_str().and_then(parse_id) else { return };
                let data = cmd["data"].as_str().unwrap_or("").as_bytes().to_vec();
                if let Some(peer) = shared.peers.lock().unwrap().get(&id) {
                    let _ = peer.tx.send(data);
                }
            }
            "close" => {
                let Some(id) = cmd["id"].as_str().and_then(parse_id) else { return };
                if let Some(peer) = shared.peers.lock().unwrap().get(&id) {
                    peer.conn.close(0u32.into(), b"bye");
                }
            }
            "pair_open" => {
                let code = normalize(cmd["code"].as_str().unwrap_or(""));
                let plate = nameplate(&code);
                *shared.pairing.lock().unwrap() = Some(Pairing { code, info: cmd["info"].clone(), busy: false });
                ep.set_user_data_for_address_lookup(UserData::try_from(format!("pair={plate}")).ok());
            }
            "pair_close" => {
                *shared.pairing.lock().unwrap() = None;
                ep.set_user_data_for_address_lookup(None);
            }
            "pair_join" => {
                let code = normalize(cmd["code"].as_str().unwrap_or(""));
                let at = cmd["at"].as_str().filter(|s| !s.is_empty()).map(String::from);
                let info = cmd["info"].clone();
                self.runtime.spawn(async move {
                    if let Err(reason) = pair_as_guest(shared.clone(), ep, code, at, info).await {
                        shared.emit(json!({"ev": "pair_failed", "reason": reason}));
                    }
                });
            }
            "pair_answer" => {
                if let Some(tx) = shared.answer.lock().unwrap().take() {
                    let _ = tx.send(cmd["accept"].as_bool().unwrap_or(false));
                }
            }
            _ => {}
        }
    }

    fn poll(&self) -> Option<String> {
        let mut drain = [0u8; 64];
        // SAFETY: read_fd is this link's non-blocking pipe.
        while unsafe { libc::read(self.read_fd, drain.as_mut_ptr().cast(), drain.len()) } > 0 {}
        self.shared.events.lock().unwrap().pop_front()
    }
}

impl Drop for Link {
    fn drop(&mut self) {
        let ep = self.endpoint.clone();
        self.runtime.block_on(async move {
            let _ = tokio::time::timeout(Duration::from_secs(2), ep.close()).await;
        });
        unsafe {
            libc::close(self.read_fd);
            libc::close(self.shared.wake);
        }
    }
}

async fn resolve(host: &str, default_port: u16) -> Vec<SocketAddr> {
    if let Ok(addr) = host.parse::<SocketAddr>() {
        return vec![addr];
    }
    let target = if host.rsplit_once(':').is_some_and(|(h, p)| !h.contains(':') && p.parse::<u16>().is_ok()) {
        host.to_string()
    } else if host.contains(':') && !host.starts_with('[') {
        format!("[{host}]:{default_port}")
    } else {
        format!("{host}:{default_port}")
    };
    match tokio::time::timeout(Duration::from_secs(5), tokio::net::lookup_host(target)).await {
        Ok(Ok(addrs)) => addrs.collect(),
        _ => vec![],
    }
}

/// One connection to a paired machine: a single stream each way of
/// length-prefixed frames. If both ends dial at once, both keep the
/// connection started by the lower key and close the other.
async fn run_peer(shared: Arc<Shared>, conn: Connection, outgoing: bool) {
    let id = conn.remote_id();
    let initiator = if outgoing { shared.me } else { id };
    let streams = if outgoing {
        match conn.open_bi().await {
            Ok((mut send, recv)) => match write_frame(&mut send, b"").await {
                Ok(()) => Some((send, recv)),
                Err(_) => None,
            },
            Err(_) => None,
        }
    } else {
        conn.accept_bi().await.ok()
    };
    let Some((mut send, mut recv)) = streams else { return };

    let (tx, mut rx) = mpsc::unbounded_channel::<Vec<u8>>();
    let stable = conn.stable_id();
    {
        let mut peers = shared.peers.lock().unwrap();
        if let Some(existing) = peers.get(&id) {
            let keep_existing = existing.initiator.as_bytes() <= initiator.as_bytes() && existing.conn.stable_id() != stable;
            if keep_existing {
                drop(peers);
                conn.close(0u32.into(), b"duplicate");
                return;
            }
            existing.conn.close(0u32.into(), b"duplicate");
        }
        peers.insert(id, Peer { tx, conn: conn.clone(), initiator });
    }
    let (kind, addrs) = path_of(&conn);
    shared.emit(json!({"ev": "up", "id": id_hex(&id), "path": kind, "addrs": addrs}));

    let writer = tokio::spawn(async move {
        while let Some(data) = rx.recv().await {
            if write_frame(&mut send, &data).await.is_err() {
                break;
            }
        }
        let _ = send.finish();
    });

    let watch = {
        let conn = conn.clone();
        let shared = shared.clone();
        tokio::spawn(async move {
            let mut last = String::new();
            let mut stream = conn.paths_stream();
            while stream.next().await.is_some() {
                let (kind, addrs) = path_of(&conn);
                if kind != last {
                    last = kind.clone();
                    shared.emit(json!({"ev": "path", "id": id_hex(&conn.remote_id()), "path": kind, "addrs": addrs}));
                }
            }
        })
    };

    loop {
        match read_frame(&mut recv).await {
            Ok(frame) if frame.is_empty() => continue,
            Ok(frame) => match String::from_utf8(frame) {
                Ok(text) => shared.emit(json!({"ev": "msg", "id": id_hex(&id), "data": text})),
                Err(_) => break,
            },
            Err(_) => break,
        }
    }
    conn.close(0u32.into(), b"done");
    writer.abort();
    watch.abort();
    let mut peers = shared.peers.lock().unwrap();
    if peers.get(&id).is_some_and(|p| p.conn.stable_id() == stable) {
        peers.remove(&id);
        drop(peers);
        shared.emit(json!({"ev": "down", "id": id_hex(&id)}));
    }
}

/// The waiting side of pairing. The code works once: a wrong one ends the
/// pairing as surely as a right one, so it can't be guessed at.
async fn pair_as_host(shared: Arc<Shared>, ep: Endpoint, conn: Connection) -> anyhow::Result<()> {
    let guest = conn.remote_id();
    let (code, info) = {
        let mut pairing = shared.pairing.lock().unwrap();
        match pairing.as_mut() {
            Some(p) if !p.busy => {
                p.busy = true;
                (p.code.clone(), p.info.clone())
            }
            _ => {
                conn.close(3u32.into(), b"not pairing");
                return Ok(());
            }
        }
    };
    let finish = |shared: &Arc<Shared>, ep: &Endpoint| {
        *shared.pairing.lock().unwrap() = None;
        ep.set_user_data_for_address_lookup(None);
    };
    let (mut send, mut recv) = conn.accept_bi().await?;
    let result: anyhow::Result<()> = async {
        let first = read_json(&mut recv).await?;
        let (state, reply) = Spake2::<Ed25519Group>::start_b(
            &Password::new(code.as_bytes()),
            &Identity::new(guest.as_bytes()),
            &Identity::new(shared.me.as_bytes()),
        );
        write_json(&mut send, &json!({"spake": b64(&reply)})).await?;
        let key = state.finish(&unb64(&first["spake"])?).map_err(|_| anyhow::anyhow!("bad exchange"))?;
        let theirs = unb64(&read_json(&mut recv).await?["confirm"])?;
        if theirs != confirmation(&key, "joiner", &guest, &shared.me) {
            finish(&shared, &ep);
            shared.emit(json!({"ev": "pair_failed", "reason": "wrong_code"}));
            let _ = write_json(&mut send, &json!({"error": "wrong_code"})).await;
            let _ = send.finish();
            let _ = tokio::time::timeout(Duration::from_secs(5), conn.closed()).await;
            return Ok(());
        }
        write_json(
            &mut send,
            &json!({"confirm": b64(&confirmation(&key, "pairer", &guest, &shared.me)), "info": info}),
        )
        .await?;
        let answer = tokio::time::timeout(Duration::from_secs(600), read_json(&mut recv)).await??;
        finish(&shared, &ep);
        if answer["accept"].as_bool() != Some(true) {
            shared.emit(json!({"ev": "pair_failed", "reason": "declined"}));
            return Ok(());
        }
        write_json(&mut send, &json!({"ok": true})).await?;
        let _ = send.finish();
        let (_, addrs) = path_of(&conn);
        shared.emit(json!({"ev": "paired", "id": id_hex(&guest), "info": answer["info"], "addrs": addrs}));
        // Let the last frame arrive before the connection goes.
        let _ = tokio::time::timeout(Duration::from_secs(5), conn.closed()).await;
        Ok(())
    }
    .await;
    if result.is_err() {
        // Gave up part way, as a dropped connection does: the code is spent.
        let was_open = shared.pairing.lock().unwrap().is_some();
        if was_open {
            finish(&shared, &ep);
            shared.emit(json!({"ev": "pair_failed", "reason": "interrupted"}));
        }
    }
    result
}

async fn find_host(shared: &Arc<Shared>, code: &str, at: Option<String>) -> Result<EndpointAddr, String> {
    if let Some(at) = at {
        let addrs = resolve(&at, shared.default_port).await;
        if addrs.is_empty() {
            return Err("unknown_host".into());
        }
        for addr in &addrs {
            let Ok(Ok(stream)) = tokio::time::timeout(Duration::from_secs(5), TcpStream::connect(addr)).await else {
                continue;
            };
            let mut line = String::new();
            let mut reader = BufReader::new(stream);
            if tokio::time::timeout(Duration::from_secs(5), reader.read_line(&mut line)).await.is_err() {
                continue;
            }
            let answer: Value = serde_json::from_str(&line).unwrap_or(Value::Null);
            if answer["pairing"].as_bool() != Some(true) {
                return Err("not_waiting".into());
            }
            let Some(id) = answer["id"].as_str().and_then(parse_id) else { continue };
            return Ok(EndpointAddr::from_parts(id, addrs.iter().map(|a| TransportAddr::Ip(*a))));
        }
        return Err("unreachable".into());
    }
    let wanted = format!("pair={}", nameplate(code));
    for _ in 0..40 {
        let found = shared
            .seen
            .lock()
            .unwrap()
            .iter()
            .find(|(_, data)| data.as_deref() == Some(wanted.as_str()))
            .map(|(id, _)| *id);
        if let Some(id) = found {
            return Ok(EndpointAddr::new(id));
        }
        tokio::time::sleep(Duration::from_millis(250)).await;
    }
    Err("not_found".into())
}

async fn pair_as_guest(
    shared: Arc<Shared>,
    ep: Endpoint,
    code: String,
    at: Option<String>,
    info: Value,
) -> Result<(), String> {
    let addr = find_host(&shared, &code, at).await?;
    let host = addr.id;
    if host == shared.me {
        return Err("self".into());
    }
    let conn = tokio::time::timeout(Duration::from_secs(15), ep.connect(addr, PAIR_ALPN))
        .await
        .map_err(|_| "unreachable".to_string())?
        .map_err(|_| "unreachable".to_string())?;
    let io = |e: anyhow::Error| {
        let _ = e;
        "interrupted".to_string()
    };
    let (mut send, mut recv) = conn.open_bi().await.map_err(|e| io(e.into()))?;
    let (state, first) = Spake2::<Ed25519Group>::start_a(
        &Password::new(code.as_bytes()),
        &Identity::new(shared.me.as_bytes()),
        &Identity::new(host.as_bytes()),
    );
    write_json(&mut send, &json!({"spake": b64(&first)})).await.map_err(io)?;
    let reply = read_json(&mut recv).await.map_err(io)?;
    let key = state
        .finish(&unb64(&reply["spake"]).map_err(io)?)
        .map_err(|_| "interrupted".to_string())?;
    write_json(&mut send, &json!({"confirm": b64(&confirmation(&key, "joiner", &shared.me, &host))}))
        .await
        .map_err(io)?;
    let theirs = read_json(&mut recv).await.map_err(io)?;
    if theirs["error"].as_str() == Some("wrong_code") {
        return Err("wrong_code".into());
    }
    if unb64(&theirs["confirm"]).map_err(io)? != confirmation(&key, "pairer", &shared.me, &host) {
        return Err("wrong_code".into());
    }
    let (tx, rx) = oneshot::channel();
    *shared.answer.lock().unwrap() = Some(tx);
    shared.emit(json!({"ev": "pair_confirm", "id": id_hex(&host), "info": theirs["info"]}));
    let accept = tokio::time::timeout(Duration::from_secs(600), rx).await.ok().and_then(|r| r.ok()).unwrap_or(false);
    write_json(&mut send, &json!({"accept": accept, "info": if accept { info } else { Value::Null }}))
        .await
        .map_err(io)?;
    if !accept {
        let _ = send.finish();
        return Err("declined".into());
    }
    let done = read_json(&mut recv).await.map_err(io)?;
    if done["ok"].as_bool() != Some(true) {
        return Err("interrupted".into());
    }
    let (_, addrs) = path_of(&conn);
    shared.emit(json!({"ev": "paired", "id": id_hex(&host), "info": theirs["info"], "addrs": addrs}));
    conn.close(0u32.into(), b"paired");
    Ok(())
}

// The C interface. Strings in are UTF-8 and stay the caller's; strings out
// are the link's until handed back to needful_link_free_string.

/// Starts a link from a JSON config: {"secret": hex, "port": n, "relay": url,
/// "mdns": bool, "loopback": bool}. Returns null and sets *error on failure.
#[no_mangle]
pub extern "C" fn needful_link_start(config: *const c_char, error: *mut *mut c_char) -> *mut Link {
    let text = unsafe { CStr::from_ptr(config) }.to_string_lossy().into_owned();
    let result = serde_json::from_str::<Config>(&text).map_err(anyhow::Error::from).and_then(Link::start);
    match result {
        Ok(link) => Box::into_raw(Box::new(link)),
        Err(e) => {
            if !error.is_null() {
                unsafe { *error = CString::new(e.to_string()).unwrap_or_default().into_raw() };
            }
            std::ptr::null_mut()
        }
    }
}

/// Readable whenever there are events to take with needful_link_poll.
#[no_mangle]
pub extern "C" fn needful_link_event_fd(link: *mut Link) -> c_int {
    unsafe { &*link }.read_fd
}

/// The next event as JSON, or null when there are none.
#[no_mangle]
pub extern "C" fn needful_link_poll(link: *mut Link) -> *mut c_char {
    match unsafe { &*link }.poll() {
        Some(text) => CString::new(text).unwrap_or_default().into_raw(),
        None => std::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "C" fn needful_link_command(link: *mut Link, command: *const c_char) {
    let text = unsafe { CStr::from_ptr(command) }.to_string_lossy();
    unsafe { &*link }.command(&text);
}

#[no_mangle]
pub extern "C" fn needful_link_free_string(text: *mut c_char) {
    if !text.is_null() {
        drop(unsafe { CString::from_raw(text) });
    }
}

#[no_mangle]
pub extern "C" fn needful_link_stop(link: *mut Link) {
    if !link.is_null() {
        drop(unsafe { Box::from_raw(link) });
    }
}

/// The public key for a secret key, both as hex, for showing a fingerprint
/// without starting a link. Returns null for a bad key.
#[no_mangle]
pub extern "C" fn needful_link_public_key(secret: *const c_char) -> *mut c_char {
    let text = unsafe { CStr::from_ptr(secret) }.to_string_lossy();
    let Ok(bytes) = hex::decode(text.as_ref()) else { return std::ptr::null_mut() };
    let Ok(bytes) = <[u8; 32]>::try_from(bytes) else { return std::ptr::null_mut() };
    let public = SecretKey::from_bytes(&bytes).public();
    CString::new(id_hex(&public)).unwrap_or_default().into_raw()
}
