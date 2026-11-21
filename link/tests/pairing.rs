use std::ffi::{CStr, CString};
use std::time::{Duration, Instant};

use needful_link::*;
use serde_json::{json, Value};

struct L(*mut Link);

impl L {
    fn start(secret: &str) -> L {
        let config = CString::new(json!({"secret": secret, "port": 0, "mdns": false, "loopback": true}).to_string()).unwrap();
        let mut error = std::ptr::null_mut();
        let link = needful_link_start(config.as_ptr(), &mut error);
        assert!(!link.is_null(), "start failed");
        L(link)
    }
    fn cmd(&self, value: Value) {
        let text = CString::new(value.to_string()).unwrap();
        needful_link_command(self.0, text.as_ptr());
    }
    fn wait(&self, ev: &str) -> Value {
        let deadline = Instant::now() + Duration::from_secs(20);
        while Instant::now() < deadline {
            let p = needful_link_poll(self.0);
            if p.is_null() {
                std::thread::sleep(Duration::from_millis(20));
                continue;
            }
            let v: Value = serde_json::from_str(unsafe { CStr::from_ptr(p) }.to_str().unwrap()).unwrap();
            needful_link_free_string(p);
            if v["ev"] == ev {
                return v;
            }
        }
        panic!("no {ev} event");
    }
}

impl Drop for L {
    fn drop(&mut self) {
        needful_link_stop(self.0);
    }
}

#[test]
fn pair_then_talk() {
    let a = L::start(&"11".repeat(32));
    let b = L::start(&"22".repeat(32));
    let ra = a.wait("ready");
    let rb = b.wait("ready");
    let port = ra["port"].as_u64().unwrap();

    a.cmd(json!({"op": "pair_open", "code": "7-violet-crumble-tango", "info": {"name": "desktop"}}));
    b.cmd(json!({"op": "pair_join", "code": "7-Violet-Crumble-Tango ", "at": format!("127.0.0.1:{port}"), "info": {"name": "laptop"}}));
    let confirm = b.wait("pair_confirm");
    assert_eq!(confirm["info"]["name"], "desktop");
    assert_eq!(confirm["id"], ra["id"]);
    b.cmd(json!({"op": "pair_answer", "accept": true}));
    let pa = a.wait("paired");
    assert_eq!(pa["info"]["name"], "laptop");
    assert_eq!(pa["id"], rb["id"]);
    let pb = b.wait("paired");
    assert_eq!(pb["id"], ra["id"]);

    a.cmd(json!({"op": "trust", "ids": [rb["id"]]}));
    b.cmd(json!({"op": "trust", "ids": [ra["id"]]}));
    b.cmd(json!({"op": "dial", "id": ra["id"], "addrs": [format!("127.0.0.1:{port}")]}));
    let up = a.wait("up");
    assert_eq!(up["path"], "local");
    b.wait("up");
    b.cmd(json!({"op": "send", "id": ra["id"], "data": "hello"}));
    let msg = a.wait("msg");
    assert_eq!(msg["data"], "hello");
}

#[test]
fn wrong_code_spends_the_code() {
    let a = L::start(&"33".repeat(32));
    let b = L::start(&"44".repeat(32));
    let port = a.wait("ready")["port"].as_u64().unwrap();
    b.wait("ready");
    a.cmd(json!({"op": "pair_open", "code": "3-apple-bird-cloud", "info": {}}));
    b.cmd(json!({"op": "pair_join", "code": "3-apple-bird-clown", "at": format!("127.0.0.1:{port}"), "info": {}}));
    assert_eq!(b.wait("pair_failed")["reason"], "wrong_code");
    assert_eq!(a.wait("pair_failed")["reason"], "wrong_code");
    // Used once, it's gone, even with the right words now.
    b.cmd(json!({"op": "pair_join", "code": "3-apple-bird-cloud", "at": format!("127.0.0.1:{port}"), "info": {}}));
    assert_eq!(b.wait("pair_failed")["reason"], "not_waiting");
}

#[test]
fn strangers_cannot_connect() {
    let a = L::start(&"55".repeat(32));
    let b = L::start(&"66".repeat(32));
    let ra = a.wait("ready");
    b.wait("ready");
    b.cmd(json!({"op": "trust", "ids": [ra["id"]]}));
    b.cmd(json!({"op": "dial", "id": ra["id"], "addrs": [format!("127.0.0.1:{}", ra["port"])]}));
    // a trusts nobody, so b's connection is closed as soon as it's made.
    let deadline = Instant::now() + Duration::from_secs(5);
    while Instant::now() < deadline {
        let p = needful_link_poll(a.0);
        if !p.is_null() {
            let v: Value = serde_json::from_str(unsafe { CStr::from_ptr(p) }.to_str().unwrap()).unwrap();
            needful_link_free_string(p);
            assert_ne!(v["ev"], "up");
        }
        std::thread::sleep(Duration::from_millis(20));
    }
}

// Needs a network that carries multicast, so it's run by hand:
// cargo test -- --ignored
#[test]
#[ignore]
fn pair_on_the_same_network_by_code_alone() {
    let start = |secret: &str| {
        let config = CString::new(json!({"secret": secret, "port": 0, "mdns": true}).to_string()).unwrap();
        let link = needful_link_start(config.as_ptr(), std::ptr::null_mut());
        assert!(!link.is_null());
        L(link)
    };
    let a = start(&"77".repeat(32));
    let b = start(&"88".repeat(32));
    let ra = a.wait("ready");
    b.wait("ready");
    a.cmd(json!({"op": "pair_open", "code": "12-one-two-three", "info": {"name": "desktop"}}));
    std::thread::sleep(Duration::from_secs(2));
    b.cmd(json!({"op": "pair_join", "code": "12-one-two-three", "info": {"name": "laptop"}}));
    assert_eq!(b.wait("pair_confirm")["id"], ra["id"]);
    b.cmd(json!({"op": "pair_answer", "accept": true}));
    a.wait("paired");
    b.wait("paired");
}

/// Two computers that can't find each other, one waiting at a relay: the
/// code it shows carries where to find it, so the other joins by code alone.
fn local_relay(runtime: &tokio::runtime::Runtime) -> (iroh_relay::server::Server, String) {
    use iroh_relay::server::{RelayConfig, Server, ServerConfig};
    let server = runtime.block_on(async {
        let mut config = ServerConfig::default();
        config.relay = Some(RelayConfig::new(([127, 0, 0, 1], 0)));
        Server::spawn(config).await.unwrap()
    });
    let url = format!("http://{}", server.http_addr().unwrap());
    (server, url)
}

#[test]
fn pair_through_a_relay_by_code_alone() {
    pair_through_relay(false);
}

/// The joiner has a relay of its own, like the public one, and still finds
/// the waiting computer at the one in the code.
#[test]
fn pair_through_the_codes_relay_from_another() {
    pair_through_relay(true);
}

fn pair_through_relay(joiner_has_its_own: bool) {
    let runtime = tokio::runtime::Runtime::new().unwrap();
    let (server, relay) = local_relay(&runtime);
    let (other, other_url) = local_relay(&runtime);

    let start = |secret: &str, relay: &str| {
        let config =
            json!({"secret": secret, "port": 0, "mdns": false, "loopback": true, "relay": relay}).to_string();
        let config = CString::new(config).unwrap();
        let link = needful_link_start(config.as_ptr(), std::ptr::null_mut());
        assert!(!link.is_null());
        L(link)
    };
    let a = start(&"99".repeat(32), &relay);
    let ra = a.wait("ready");
    let words = CString::new("21-maple-otter-quilt").unwrap();
    let code = needful_link_pair_code(a.0, words.as_ptr());
    let code_text = unsafe { CStr::from_ptr(code) }.to_str().unwrap().to_string();
    needful_link_free_string(code);
    assert!(code_text.starts_with("21-maple-otter-quilt."), "{code_text}");

    // What the joiner borrows to get there.
    let code_c = CString::new(code_text.clone()).unwrap();
    let named = needful_link_code_relay(code_c.as_ptr());
    let named_text = unsafe { CStr::from_ptr(named) }.to_str().unwrap().to_string();
    needful_link_free_string(named);
    assert_eq!(named_text.trim_end_matches('/'), relay);

    let b = start(&"aa".repeat(32), if joiner_has_its_own { &other_url } else { &named_text });
    let rb = b.wait("ready");
    a.cmd(json!({"op": "pair_open", "code": code_text, "info": {"name": "desktop"}}));
    // Typed with the words in capitals, as people do.
    let (head, tail) = code_text.split_once('.').unwrap();
    b.cmd(json!({"op": "pair_join", "code": format!("{}.{tail}", head.to_uppercase()), "info": {"name": "laptop"}}));
    assert_eq!(b.wait("pair_confirm")["id"], ra["id"]);
    b.cmd(json!({"op": "pair_answer", "accept": true}));
    assert_eq!(a.wait("paired")["id"], rb["id"]);
    b.wait("paired");
    drop((a, b));
    drop((server, other));
}

#[test]
fn a_cut_off_code_says_so() {
    let b = L::start(&"bb".repeat(32));
    b.wait("ready");
    b.cmd(json!({"op": "pair_join", "code": "4-one-two-three.AAAA", "info": {}}));
    assert_eq!(b.wait("pair_failed")["reason"], "bad_code");
    let plain = CString::new("4-one-two-three").unwrap();
    assert!(needful_link_code_relay(plain.as_ptr()).is_null());
}
