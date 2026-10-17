mod nl;

use clap::{Args, Parser, Subcommand};
use nl::{put_attr, Attr, Frame, Genl};
use serde_json::{json, Map, Value};
use std::fs;
use std::io::{self, Write};
use std::path::Path;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

const AIC_OUI: u32 = 0x414943;
const DRIVER: &str = "aic8800_fdrv";
const NL80211_CMD_VENDOR: u8 = 103;
const NL80211_ATTR_WIPHY: u16 = 1;
const NL80211_ATTR_IFINDEX: u16 = 3;
const NL80211_ATTR_VENDOR_ID: u16 = 195;
const NL80211_ATTR_VENDOR_SUBCMD: u16 = 196;
const NL80211_ATTR_VENDOR_DATA: u16 = 197;

const SUBCMD_COEX_GET: u32 = 1;
const SUBCMD_COEX_SET: u32 = 2;
const SUBCMD_GET_INFO: u32 = 3;
const SUBCMD_GET_STATS: u32 = 4;

const EVENTS: [&str; 3] = ["COEX_TS_TIMEOUT", "FW_PANIC", "FW_ASSERT"];

#[derive(Clone, Copy)]
enum Kind {
    Flag,
    U8,
    I8,
    U32,
    U64,
    Hex32,
    Str,
}

// (id, nome JSON, etichetta, tipo, unita'); ordine e numeri come in docs/interfaccia-vendor.md.
const ATTRS: &[(u16, &str, &str, Kind, &str)] = &[
    (1, "bt_on", "Bluetooth attivo", Kind::Flag, ""),
    (2, "disable_coexnull", "Coexnull disabilitato", Kind::Flag, ""),
    (3, "enable_nullcts", "Null-CTS abilitato", Kind::Flag, ""),
    (4, "enable_periodic_timer", "Timer periodico", Kind::Flag, ""),
    (5, "timeslot_set", "Timeslot abilitati", Kind::Flag, ""),
    (6, "timeslot0", "Timeslot 0", Kind::U32, ""),
    (7, "timeslot1", "Timeslot 1", Kind::U32, ""),
    (8, "applied", "Inviato al firmware", Kind::Flag, ""),
    (9, "driver_version", "Driver", Kind::Str, ""),
    (10, "fw_version", "Firmware", Kind::Str, ""),
    (11, "chip_id", "Chip (PRODUCT_ID)", Kind::U8, ""),
    (12, "band_5g", "Banda 5 GHz", Kind::Flag, ""),
    (13, "if_running", "Interfacce aperte", Kind::U8, ""),
    (14, "temp_c", "Temperatura", Kind::I8, " C"),
    (15, "chan_freq", "Canale primario", Kind::U32, " MHz"),
    (16, "chan_center_freq1", "Frequenza centrale", Kind::U32, " MHz"),
    (17, "chan_width", "Larghezza", Kind::U32, " MHz"),
    (18, "rx_packets", "RX pacchetti", Kind::U64, ""),
    (19, "tx_packets", "TX pacchetti", Kind::U64, ""),
    (20, "rx_bytes", "RX byte", Kind::U64, ""),
    (21, "tx_bytes", "TX byte", Kind::U64, ""),
    (22, "rx_errors", "RX errori", Kind::U64, ""),
    (23, "tx_errors", "TX errori", Kind::U64, ""),
    (24, "rx_dropped", "RX scartati", Kind::U64, ""),
    (25, "tx_dropped", "TX scartati", Kind::U64, ""),
    (26, "sta_rssi", "RSSI", Kind::I8, " dBm"),
    (27, "sta_rate_info", "Rate (grezzo)", Kind::Hex32, ""),
    (28, "sta_tx_failed", "TX falliti", Kind::U32, ""),
    (29, "sta_chan_time", "Tempo canale", Kind::U32, ""),
    (30, "sta_chan_busy_time", "Canale occupato", Kind::U32, ""),
    (31, "sta_chan_tx_busy_time", "Canale occupato da TX", Kind::U32, ""),
    (32, "sta_ack_fail", "ACK mancati", Kind::U32, ""),
    (33, "sta_ack_succ", "ACK ricevuti", Kind::U32, ""),
];

#[derive(Parser)]
#[command(version, about = "Controllo della chiavetta AIC8800D80 via interfaccia vendor nl80211 di aic8800_fdrv")]
struct Cli {
    /// Uscita JSON (un oggetto; per `events` un oggetto per riga)
    #[arg(long, global = true)]
    json: bool,
    /// Interfaccia (wlan0) o wiphy (phy1); di default il primo gestito da aic8800_fdrv
    #[arg(long, short, global = true)]
    dev: Option<String>,
    #[command(subcommand)]
    cmd: Cmd,
}

#[derive(Subcommand)]
enum Cmd {
    /// Versioni, chip, temperatura, canale
    Info,
    /// Contatori di rete, canale e stazione (richiede l'interfaccia aperta)
    Stats,
    /// Coesistenza Wi-Fi/Bluetooth
    Coex {
        #[command(subcommand)]
        op: CoexOp,
    },
    /// Ascolta gli eventi vendor AIC (COEX_TS_TIMEOUT, FW_PANIC, FW_ASSERT) fino a Ctrl-C
    Events,
}

#[derive(Subcommand)]
enum CoexOp {
    Get,
    /// Imposta solo i valori indicati (richiede CAP_NET_ADMIN)
    Set(SetArgs),
}

#[derive(Args)]
struct SetArgs {
    #[arg(long, value_name = "0|1", value_parser = parse_bool)]
    bt_on: Option<bool>,
    #[arg(long, value_name = "0|1", value_parser = parse_bool)]
    disable_coexnull: Option<bool>,
    #[arg(long, value_name = "0|1", value_parser = parse_bool)]
    enable_nullcts: Option<bool>,
    #[arg(long, value_name = "0|1", value_parser = parse_bool)]
    periodic_timer: Option<bool>,
    /// Abilita o spegne i due timeslot (con --timeslot si abilitano da soli)
    #[arg(long, value_name = "0|1", value_parser = parse_bool)]
    timeslot_set: Option<bool>,
    /// Valori dei timeslot (significato e unita' non documentati: sperimentale)
    #[arg(long, value_name = "A,B", value_parser = parse_slots)]
    timeslot: Option<(u32, u32)>,
}

fn parse_bool(s: &str) -> Result<bool, String> {
    match s {
        "1" | "on" | "true" | "si" => Ok(true),
        "0" | "off" | "false" | "no" => Ok(false),
        _ => Err(format!("`{s}` non e' un booleano (usa 0/1, on/off)")),
    }
}

fn parse_slots(s: &str) -> Result<(u32, u32), String> {
    let (a, b) = s.split_once(',').ok_or("atteso A,B (due interi)")?;
    let n = |x: &str| x.trim().parse::<u32>().map_err(|_| format!("`{x}` non e' un intero senza segno"));
    Ok((n(a)?, n(b)?))
}

struct Target {
    phy: String,
    wiphy: u32,
    ifaces: Vec<(String, u32)>,
}

fn read_u32(p: impl AsRef<Path>) -> Option<u32> {
    fs::read_to_string(p).ok()?.trim().parse().ok()
}

fn phys() -> Vec<(String, u32)> {
    let mut v: Vec<_> = fs::read_dir("/sys/class/ieee80211")
        .into_iter()
        .flatten()
        .flatten()
        .filter_map(|e| {
            let name = e.file_name().to_string_lossy().into_owned();
            let idx = read_u32(e.path().join("index"))?;
            Some((name, idx))
        })
        .collect();
    v.sort();
    v
}

fn phy_name(wiphy: u32) -> String {
    phys().into_iter().find(|p| p.1 == wiphy).map_or_else(|| format!("wiphy{wiphy}"), |p| p.0)
}

fn ifaces_of(phy: &str) -> Vec<(String, u32)> {
    let mut v: Vec<_> = fs::read_dir(format!("/sys/class/ieee80211/{phy}/device/net"))
        .into_iter()
        .flatten()
        .flatten()
        .filter_map(|e| {
            let n = e.file_name().to_string_lossy().into_owned();
            let i = read_u32(format!("/sys/class/net/{n}/ifindex"))?;
            Some((n, i))
        })
        .collect();
    v.sort();
    v
}

// Per USB `device` e' il dispositivo intero (driver `usb`): il driver sta sulle sue interfacce (4-4:1.x).
fn driven_by(phy: &str, driver: &str) -> bool {
    let dev = format!("/sys/class/ieee80211/{phy}/device");
    fs::read_dir(&dev).into_iter().flatten().flatten().any(|e| {
        fs::read_link(e.path().join("driver")).ok().and_then(|l| l.file_name().map(|n| n == driver)).unwrap_or(false)
    }) || fs::read_link(format!("{dev}/driver")).ok().and_then(|l| l.file_name().map(|n| n == driver)).unwrap_or(false)
}

fn discover(dev: Option<&str>) -> Result<Target, String> {
    let all = phys();
    if let Some(d) = dev {
        if let Some((phy, wiphy)) = all.iter().find(|p| p.0 == d) {
            return Ok(Target { phy: phy.clone(), wiphy: *wiphy, ifaces: ifaces_of(phy) });
        }
        let link = fs::read_link(format!("/sys/class/net/{d}/phy80211"))
            .map_err(|_| format!("`{d}` non e' ne' un wiphy ne' un'interfaccia wireless"))?;
        let phy = link.file_name().unwrap().to_string_lossy().into_owned();
        let wiphy = all.iter().find(|p| p.0 == phy).map(|p| p.1).ok_or("wiphy non trovato in sysfs")?;
        let idx = read_u32(format!("/sys/class/net/{d}/ifindex")).ok_or("ifindex non leggibile")?;
        return Ok(Target { phy, wiphy, ifaces: vec![(d.to_string(), idx)] });
    }
    match all.into_iter().find(|p| driven_by(&p.0, DRIVER)) {
        Some((phy, wiphy)) => Ok(Target { ifaces: ifaces_of(&phy), phy, wiphy }),
        None => Err(format!(
            "nessun wiphy gestito da {DRIVER}: modulo non caricato (`modprobe {DRIVER}`), chiavetta non inserita \
             o ancora in modalita' CD-ROM (1111:1111)"
        )),
    }
}

fn explain(e: &io::Error, ctx: &str) -> String {
    let hint = match e.raw_os_error() {
        Some(libc::EPERM) | Some(libc::EACCES) => "permessi insufficienti: i comandi vendor richiedono CAP_NET_ADMIN (usa sudo)".to_string(),
        Some(libc::ENETDOWN) => "interfaccia non aperta, quindi firmware non avviato: `ip link set <if> up`".to_string(),
        Some(libc::ENODEV) => "wiphy sparito (il nome phyN cambia a ogni ricarica del modulo): riprova".to_string(),
        Some(libc::EOPNOTSUPP) => "il driver caricato non ha l'interfaccia vendor AIC (modulo vecchio)".to_string(),
        Some(libc::EINVAL) => "valore rifiutato dal driver (EINVAL)".to_string(),
        Some(libc::ETIMEDOUT) | Some(libc::EAGAIN) => "il driver non risponde (timeout)".to_string(),
        _ => e.to_string(),
    };
    format!("{ctx}: {hint}")
}

fn flat(attrs: &[Attr]) -> Vec<&Attr> {
    attrs.iter().filter(|a| ATTRS.iter().any(|t| t.0 == a.ty)).collect()
}

fn value(kind: Kind, d: &[u8]) -> Option<Value> {
    Some(match kind {
        Kind::Flag => json!(*d.first()? != 0),
        Kind::U8 => json!(*d.first()?),
        Kind::I8 => json!(*d.first()? as i8),
        Kind::U32 => json!(u32::from_ne_bytes(d.get(..4)?.try_into().ok()?)),
        Kind::Hex32 => json!(u32::from_ne_bytes(d.get(..4)?.try_into().ok()?)),
        Kind::U64 => json!(u64::from_ne_bytes(d.get(..8)?.try_into().ok()?)),
        Kind::Str => json!(String::from_utf8_lossy(d).trim_end_matches('\0')),
    })
}

fn to_map(attrs: &[Attr]) -> Map<String, Value> {
    let mut m = Map::new();
    for a in flat(attrs) {
        let t = ATTRS.iter().find(|t| t.0 == a.ty).unwrap();
        if let Some(v) = value(t.3, &a.data) {
            m.insert(t.1.to_string(), v);
        }
    }
    m
}

fn section(id: u16) -> &'static str {
    match id {
        1..=8 => "Coesistenza",
        9..=14 => "Dispositivo",
        15..=17 => "Canale",
        18..=25 => "Rete",
        _ => "Stazione",
    }
}

fn print_text(attrs: &[Attr]) {
    let mut last = "";
    for a in flat(attrs) {
        let t = ATTRS.iter().find(|t| t.0 == a.ty).unwrap();
        let Some(v) = value(t.3, &a.data) else { continue };
        let s = section(t.0);
        if s != last {
            println!("{s}");
            last = s;
        }
        let txt = match (t.3, &v) {
            (Kind::Flag, Value::Bool(b)) => if *b { "si" } else { "no" }.to_string(),
            (Kind::Hex32, Value::Number(n)) => format!("{:#010x}", n.as_u64().unwrap_or(0)),
            (_, Value::String(s)) => s.clone(),
            (_, v) => format!("{v}{}", t.4),
        };
        println!("  {:<24} {txt}", t.2);
    }
}

fn vendor(g: &mut Genl, fam: u16, t: &Target, subcmd: u32, data: &[u8], netdev: bool) -> Result<Vec<Attr>, io::Error> {
    let targets: Vec<Option<u32>> = if netdev { t.ifaces.iter().map(|i| Some(i.1)).collect() } else { vec![None] };
    if targets.is_empty() {
        return Err(io::Error::from_raw_os_error(libc::ENETDOWN));
    }
    let mut last = None;
    for ifindex in targets {
        let mut a = Vec::new();
        match ifindex {
            Some(i) => put_attr(&mut a, NL80211_ATTR_IFINDEX, &i.to_ne_bytes()),
            None => put_attr(&mut a, NL80211_ATTR_WIPHY, &t.wiphy.to_ne_bytes()),
        }
        put_attr(&mut a, NL80211_ATTR_VENDOR_ID, &AIC_OUI.to_ne_bytes());
        put_attr(&mut a, NL80211_ATTR_VENDOR_SUBCMD, &subcmd.to_ne_bytes());
        if !data.is_empty() {
            put_attr(&mut a, NL80211_ATTR_VENDOR_DATA, data);
        }
        match g.request(fam, NL80211_CMD_VENDOR, &a) {
            Ok(replies) => {
                let data = replies.into_iter().flatten().find(|x| x.ty == NL80211_ATTR_VENDOR_DATA);
                return Ok(data.map(|d| nl::parse_attrs(&d.data)).unwrap_or_default());
            }
            Err(e) => last = Some(e),
        }
    }
    Err(last.unwrap())
}

fn coex_payload(s: &SetArgs) -> Vec<u8> {
    let mut d = Vec::new();
    let flags = [
        (1, s.bt_on),
        (2, s.disable_coexnull),
        (3, s.enable_nullcts),
        (4, s.periodic_timer),
        (5, s.timeslot_set.or(s.timeslot.map(|_| true))),
    ];
    for (id, v) in flags {
        if let Some(v) = v {
            put_attr(&mut d, id, &[v as u8]);
        }
    }
    if let Some((a, b)) = s.timeslot {
        put_attr(&mut d, 6, &a.to_ne_bytes());
        put_attr(&mut d, 7, &b.to_ne_bytes());
    }
    d
}

fn run(cli: Cli) -> Result<(), String> {
    let mut g = Genl::open(Some(Duration::from_secs(5))).map_err(|e| explain(&e, "socket netlink"))?;
    let fam = g
        .family("nl80211")
        .map_err(|e| explain(&e, "famiglia nl80211 non trovata (cfg80211 non caricato?)"))?;

    if let Cmd::Events = cli.cmd {
        return events(g, fam, cli.json);
    }

    let t = discover(cli.dev.as_deref())?;
    let (subcmd, data, netdev) = match &cli.cmd {
        Cmd::Info => (SUBCMD_GET_INFO, Vec::new(), false),
        Cmd::Stats => (SUBCMD_GET_STATS, Vec::new(), true),
        Cmd::Coex { op: CoexOp::Get } => (SUBCMD_COEX_GET, Vec::new(), false),
        Cmd::Coex { op: CoexOp::Set(s) } => {
            let d = coex_payload(s);
            if d.is_empty() {
                return Err("coex set: indica almeno un valore (--bt-on, --timeslot, ...)".into());
            }
            (SUBCMD_COEX_SET, d, false)
        }
        Cmd::Events => unreachable!(),
    };
    let attrs = vendor(&mut g, fam.id, &t, subcmd, &data, netdev)
        .map_err(|e| explain(&e, &format!("{} ({})", t.phy, ["comando", "coex get", "coex set", "info", "stats"][subcmd as usize])))?;

    if cli.json {
        let mut m = to_map(&attrs);
        m.insert("phy".into(), json!(t.phy));
        println!("{}", Value::Object(m));
    } else {
        println!("{}{}", t.phy, if t.ifaces.is_empty() { String::new() } else { format!(" ({})", t.ifaces.iter().map(|i| i.0.as_str()).collect::<Vec<_>>().join(", ")) });
        print_text(&attrs);
    }
    let get = |id| attrs.iter().find(|a| a.ty == id).and_then(|a| a.data.first().copied());
    if get(13) == Some(0) {
        eprintln!("nota: nessuna interfaccia aperta, firmware non avviato (`ip link set <if> up`): temperatura e canale non disponibili");
    }
    if get(8) == Some(0) {
        eprintln!("nota: valori coex non ancora inviati al firmware; partiranno alla prima apertura dell'interfaccia");
    }
    Ok(())
}

fn events(g: Genl, fam: nl::Family, json: bool) -> Result<(), String> {
    let grp = fam.groups.iter().find(|g| g.0 == "vendor").ok_or("gruppo multicast `vendor` di nl80211 assente")?;
    g.join(grp.1).map_err(|e| explain(&e, "iscrizione al gruppo vendor"))?;
    let out = io::stdout();
    loop {
        let frames = match g.recv() {
            Ok(f) => f,
            Err(e) if e.raw_os_error() == Some(libc::EAGAIN) => continue,
            Err(e) if e.raw_os_error() == Some(libc::ENOBUFS) => {
                eprintln!("aicctl: eventi persi (buffer del socket pieno)");
                continue;
            }
            Err(e) => return Err(explain(&e, "lettura eventi")),
        };
        for f in frames {
            let Frame::Genl { cmd, attrs, .. } = f else { continue };
            if cmd != NL80211_CMD_VENDOR {
                continue;
            }
            let u32_of = |id| {
                attrs.iter().find(|a| a.ty == id).and_then(|a| Some(u32::from_ne_bytes(a.data.get(..4)?.try_into().ok()?)))
            };
            if u32_of(NL80211_ATTR_VENDOR_ID) != Some(AIC_OUI) {
                continue;
            }
            let sub = u32_of(NL80211_ATTR_VENDOR_SUBCMD).unwrap_or(u32::MAX);
            let name = EVENTS.get(sub as usize).copied().unwrap_or("SCONOSCIUTO");
            let wiphy = u32_of(NL80211_ATTR_WIPHY);
            let phy = wiphy.map(phy_name).unwrap_or_default();
            let now = SystemTime::now().duration_since(UNIX_EPOCH).unwrap_or_default().as_secs_f64();
            let mut o = out.lock();
            if json {
                let _ = writeln!(o, "{}", json!({"time": now, "phy": phy, "event": name, "subcmd": sub}));
            } else {
                let _ = writeln!(o, "[{now:.3}] {phy} {name}");
            }
            let _ = o.flush();
        }
    }
}

fn main() {
    if let Err(e) = run(Cli::parse()) {
        eprintln!("aicctl: {e}");
        std::process::exit(1);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn payload_coex_con_timeslot() {
        let s = SetArgs {
            bt_on: Some(false),
            disable_coexnull: None,
            enable_nullcts: None,
            periodic_timer: None,
            timeslot_set: None,
            timeslot: Some((10000, 2000)),
        };
        // bt_on {len 5, type 1} + pad; timeslot_set {5,5} + pad; due u32 {8,6},{8,7}
        let d = coex_payload(&s);
        assert_eq!(d.len(), 8 + 8 + 8 + 8);
        assert_eq!(&d[..8], &[5, 0, 1, 0, 0, 0, 0, 0]);
        assert_eq!(&d[8..16], &[5, 0, 5, 0, 1, 0, 0, 0]);
    }

    #[test]
    fn parser_argomenti() {
        assert_eq!(parse_slots("1,2"), Ok((1, 2)));
        assert!(parse_slots("1").is_err());
        assert!(parse_bool("2").is_err());
    }
}
