#![allow(unused_variables,unused_mut,dead_code,unreachable_code,unused_parens,non_snake_case)]
use std::borrow::Cow;
use std::cell::RefCell;
use std::fmt;
use std::ops::{Add, Sub, Mul, Div, Rem, Neg, Not};
use std::rc::Rc;

#[derive(Clone)]
pub struct SMap(pub Vec<(Cow<'static, str>, SVal)>);
impl SMap {
    #[inline] pub fn new() -> Self { SMap(Vec::new()) }
    #[inline] pub fn from_sorted(v: Vec<(Cow<'static, str>, SVal)>) -> Self { SMap(v) }
    #[inline] pub fn len(&self) -> usize { self.0.len() }
    #[inline] pub fn is_empty(&self) -> bool { self.0.is_empty() }
    #[inline] pub fn get(&self, k: &str) -> Option<&SVal> {
        if self.0.len() <= 16 {
            for (ek, ev) in &self.0 { if ek.as_ref() == k { return Some(ev); } }
            None
        } else {
            self.0.binary_search_by(|(ek, _)| ek.as_ref().cmp(k)).ok().map(|i| &self.0[i].1)
        }
    }
    #[inline] pub fn contains_key(&self, k: &str) -> bool { self.get(k).is_some() }
    #[inline] pub fn insert(&mut self, k: Cow<'static, str>, v: SVal) {
        match self.0.binary_search_by(|(ek, _)| ek.as_ref().cmp(k.as_ref())) {
            Ok(i) => self.0[i].1 = v,
            Err(i) => self.0.insert(i, (k, v)),
        }
    }
    #[inline] pub fn remove(&mut self, k: &str) -> Option<SVal> {
        self.0.binary_search_by(|(ek, _)| ek.as_ref().cmp(k)).ok().map(|i| self.0.remove(i).1)
    }
    #[inline] pub fn iter(&self) -> impl Iterator<Item = &(Cow<'static, str>, SVal)> { self.0.iter() }
    #[inline] pub fn keys(&self) -> impl Iterator<Item = &Cow<'static, str>> { self.0.iter().map(|(k, _)| k) }
    #[inline] pub fn values(&self) -> impl Iterator<Item = &SVal> { self.0.iter().map(|(_, v)| v) }
}

#[derive(Clone)]
pub enum SVal {
    Null,
    Bool(bool),
    Num(f64),
    Str(Cow<'static, str>),
    Arr(Rc<RefCell<Vec<SVal>>>),
    Map(Rc<RefCell<SMap>>),
    Func(Rc<dyn Fn(Vec<SVal>) -> SVal>),
}

impl SVal {
    #[inline] pub fn str_owned(s: String) -> SVal { SVal::Str(Cow::Owned(s)) }
    #[inline] pub fn str_lit(s: &'static str) -> SVal { SVal::Str(Cow::Borrowed(s)) }
    pub fn is_truthy(&self) -> bool {
        match self {
            SVal::Null => false,
            SVal::Bool(b) => *b,
            SVal::Num(n) => *n != 0.0,
            SVal::Str(s) => !s.is_empty(),
            SVal::Arr(a) => !a.borrow().is_empty(),
            SVal::Map(m) => !m.borrow().is_empty(),
            SVal::Func(_) => true,
        }
    }
    pub fn as_num(&self) -> f64 {
        match self { SVal::Num(n) => *n, SVal::Bool(true) => 1.0, SVal::Str(s) => s.trim().parse().unwrap_or(0.0), _ => 0.0 }
    }
    pub fn call(&self, args: Vec<SVal>) -> SVal {
        if let SVal::Func(f) = self { f(args) } else { panic!("Attempted to call non-function: {}", self) }
    }
    #[inline] pub fn get(&self, idx: SVal) -> SVal { self.get_ref(&idx) }
    #[inline] pub fn get_ref(&self, idx: &SVal) -> SVal {
        match (self, idx) {
            (SVal::Arr(a), SVal::Num(n)) => { let b = a.borrow(); let mut i = *n as isize; if i < 0 { i += b.len() as isize; } if i >= 0 { b.get(i as usize).cloned().unwrap_or(SVal::Null) } else { SVal::Null } },
            (SVal::Map(m), SVal::Str(k)) => m.borrow().get(k.as_ref()).cloned().unwrap_or(SVal::Null),
            _ => SVal::Null,
        }
    }
    #[inline] pub fn get_key(&self, k: &str) -> SVal {
        if let SVal::Map(m) = self { m.borrow().get(k).cloned().unwrap_or(SVal::Null) } else { SVal::Null }
    }
    #[inline] pub fn set(&self, idx: SVal, val: SVal) {
        match (self, idx) {
            (SVal::Arr(a), SVal::Num(n)) => { let mut b = a.borrow_mut(); let mut i = n as isize; if i < 0 { i += b.len() as isize; } if i >= 0 && (i as usize) < b.len() { b[i as usize] = val; } }
            (SVal::Map(m), SVal::Str(k)) => { m.borrow_mut().insert(k, val); }
            _ => {}
        }
    }
    #[inline] pub fn set_key(&self, k: &'static str, val: SVal) {
        if let SVal::Map(m) = self { m.borrow_mut().insert(Cow::Borrowed(k), val); }
    }
    #[inline] pub fn eq_str(&self, s: &str) -> bool { matches!(self, SVal::Str(v) if v.as_ref() == s) }
    #[inline] pub fn eq_num(&self, n: f64) -> bool { matches!(self, SVal::Num(v) if *v == n) }
    #[inline] pub fn iter_vec(&self) -> Vec<SVal> {
        match self { SVal::Arr(a) => a.borrow().clone(), _ => vec![] }
    }
}

impl fmt::Display for SVal {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            SVal::Null => write!(f, "null"),
            SVal::Bool(b) => write!(f, "{}", b),
            SVal::Num(n) => if n.fract() == 0.0 { write!(f, "{:.0}", n) } else { write!(f, "{}", n) },
            SVal::Str(s) => write!(f, "{}", s),
            SVal::Arr(a) => {
                let items: Vec<String> = a.borrow().iter().map(|v| v.to_string()).collect();
                write!(f, "[{}]", items.join(", "))
            }
            SVal::Map(m) => {
                let items: Vec<String> = m.borrow().iter().map(|(k, v)| format!("{}: {}", k, v)).collect();
                write!(f, "{{{}}}", items.join(", "))
            }
            SVal::Func(_) => write!(f, "<fn>"),
        }
    }
}

impl PartialEq for SVal {
    fn eq(&self, other: &Self) -> bool {
        match (self, other) {
            (SVal::Null, SVal::Null) => true,
            (SVal::Bool(a), SVal::Bool(b)) => a == b,
            (SVal::Num(a), SVal::Num(b)) => a == b,
            (SVal::Str(a), SVal::Str(b)) => a == b,
            _ => false,
        }
    }
}

impl Add for SVal { type Output = SVal; fn add(self, rhs: SVal) -> SVal {
    match (&self, &rhs) {
        (SVal::Str(_), _) | (_, SVal::Str(_)) => SVal::Str(Cow::Owned(format!("{}{}", self, rhs))),
        _ => SVal::Num(self.as_num() + rhs.as_num()),
    }
}}
impl Sub for SVal { type Output = SVal; fn sub(self, rhs: SVal) -> SVal { SVal::Num(self.as_num() - rhs.as_num()) } }
impl Mul for SVal { type Output = SVal; fn mul(self, rhs: SVal) -> SVal { SVal::Num(self.as_num() * rhs.as_num()) } }
impl Div for SVal { type Output = SVal; fn div(self, rhs: SVal) -> SVal { SVal::Num(self.as_num() / rhs.as_num()) } }
impl Rem for SVal { type Output = SVal; fn rem(self, rhs: SVal) -> SVal { SVal::Num(self.as_num() % rhs.as_num()) } }
impl Neg for SVal { type Output = SVal; fn neg(self) -> SVal { SVal::Num(-self.as_num()) } }
impl Not for SVal { type Output = SVal; fn not(self) -> SVal { SVal::Bool(!self.is_truthy()) } }

fn __steel_range(a: SVal, b: SVal) -> SVal {
    let start = a.as_num() as i64;
    let end = b.as_num() as i64;
    let v: Vec<SVal> = (start..end).map(|i| SVal::Num(i as f64)).collect();
    SVal::Arr(Rc::new(RefCell::new(v)))
}

#[inline] pub fn __st_len(v: &SVal) -> SVal { match v { SVal::Arr(a) => SVal::Num(a.borrow().len() as f64), SVal::Str(s) => SVal::Num(s.chars().count() as f64), SVal::Map(m) => SVal::Num(m.borrow().len() as f64), _ => SVal::Num(0.0) } }
#[inline] pub fn __st_push(a: &SVal, v: SVal) -> SVal { if let SVal::Arr(arr) = a { arr.borrow_mut().push(v); return a.clone(); } SVal::Null }
#[inline] pub fn __st_has_key(m: &SVal, k: &SVal) -> SVal { if let SVal::Map(mp) = m { if let SVal::Str(ks) = k { return SVal::Bool(mp.borrow().contains_key(ks.as_ref())); } return SVal::Bool(mp.borrow().contains_key(k.to_string().as_str())); } SVal::Bool(false) }
#[inline] pub fn __st_delete_key(m: &SVal, k: &SVal) -> SVal { if let SVal::Map(mp) = m { if let SVal::Str(ks) = k { return SVal::Bool(mp.borrow_mut().remove(ks.as_ref()).is_some()); } return SVal::Bool(mp.borrow_mut().remove(k.to_string().as_str()).is_some()); } SVal::Bool(false) }
#[inline] pub fn __st_contains(h: &SVal, n: &SVal) -> SVal { match h { SVal::Map(m) => { if let SVal::Str(ks) = n { SVal::Bool(m.borrow().contains_key(ks.as_ref())) } else { SVal::Bool(m.borrow().contains_key(n.to_string().as_str())) } } SVal::Arr(a) => SVal::Bool(a.borrow().iter().any(|x| x == n)), SVal::Str(s) => { if let SVal::Str(sub) = n { SVal::Bool(s.contains(sub.as_ref())) } else { SVal::Bool(s.contains(n.to_string().as_str())) } } _ => SVal::Bool(false) } }
#[inline] pub fn __st_starts_with(h: &SVal, p: &SVal) -> SVal { match (h, p) { (SVal::Str(hs), SVal::Str(ps)) => SVal::Bool(hs.starts_with(ps.as_ref())), _ => SVal::Bool(h.to_string().starts_with(p.to_string().as_str())) } }
#[inline] pub fn __st_index_of(h: &SVal, n: &SVal) -> SVal { match (h, n) { (SVal::Str(s), SVal::Str(sub)) => SVal::Num(s.find(sub.as_ref()).map(|bi| s[..bi].chars().count() as f64).unwrap_or(-1.0)), (SVal::Arr(a), _) => SVal::Num(a.borrow().iter().position(|x| x == n).map(|i| i as f64).unwrap_or(-1.0)), _ => SVal::Num(-1.0) } }
#[inline] pub fn __st_trim(v: &SVal) -> SVal { if let SVal::Str(s) = v { let t = s.trim(); if t.len() == s.len() { return v.clone(); } return SVal::Str(Cow::Owned(t.to_string())); } SVal::Str(Cow::Owned(v.to_string().trim().to_string())) }
#[inline] pub fn __st_upper(v: &SVal) -> SVal { if let SVal::Str(s) = v { SVal::Str(Cow::Owned(s.to_uppercase())) } else { SVal::Str(Cow::Owned(v.to_string().to_uppercase())) } }
#[inline] pub fn __st_lower(v: &SVal) -> SVal { if let SVal::Str(s) = v { SVal::Str(Cow::Owned(s.to_lowercase())) } else { SVal::Str(Cow::Owned(v.to_string().to_lowercase())) } }
#[inline] pub fn __st_split(s: &SVal, sep: &SVal) -> SVal { let sp = match sep { SVal::Str(v) => v.as_ref(), _ => "" }; let parts: Vec<SVal> = match s { SVal::Str(Cow::Borrowed(lit)) if !sp.is_empty() => lit.split(sp).map(|p| SVal::Str(Cow::Borrowed(p))).collect(), SVal::Str(v) => if sp.is_empty() { v.chars().map(|c| SVal::Str(Cow::Owned(c.to_string()))).collect() } else { v.split(sp).map(|p| SVal::Str(Cow::Owned(p.to_string()))).collect() }, _ => vec![] }; SVal::Arr(Rc::new(RefCell::new(parts))) }
#[inline] pub fn __st_join(a: &SVal, sep: &SVal) -> SVal { if let SVal::Arr(arr) = a { let sp = match sep { SVal::Str(v) => v.as_ref(), _ => "" }; return SVal::Str(Cow::Owned(arr.borrow().iter().map(|x| x.to_string()).collect::<Vec<_>>().join(sp))); } SVal::Str(Cow::Borrowed("")) }
#[inline] pub fn __st_slice(v: &SVal, s: &SVal, e: Option<&SVal>) -> SVal { let si = (s.as_num() as isize).max(0) as usize; match v { SVal::Arr(a) => { let b = a.borrow(); let ei = e.map(|x| (x.as_num() as isize).max(0) as usize).unwrap_or(b.len()).min(b.len()); let si = si.min(ei); SVal::Arr(Rc::new(RefCell::new(b[si..ei].to_vec()))) } SVal::Str(st) => { let cs: Vec<char> = st.chars().collect(); let ei = e.map(|x| (x.as_num() as isize).max(0) as usize).unwrap_or(cs.len()).min(cs.len()); let si = si.min(ei); SVal::Str(Cow::Owned(cs[si..ei].iter().collect())) } _ => SVal::Null } }
#[inline] pub fn __st_sort(v: &SVal) -> SVal { if let SVal::Arr(a) = v { let mut out = a.borrow().clone(); out.sort_by(|x, y| x.to_string().cmp(&y.to_string())); return SVal::Arr(Rc::new(RefCell::new(out))); } SVal::Null }
#[inline] pub fn __st_str(v: &SVal) -> SVal { if let SVal::Str(_) = v { v.clone() } else { SVal::Str(Cow::Owned(v.to_string())) } }
fn __st_js_raw(v: &SVal) -> String { match v { SVal::Null => "null".into(), SVal::Bool(b) => b.to_string(), SVal::Num(n) => if n.fract() == 0.0 && n.abs() < 1e15 { format!("{}", *n as i64) } else { format!("{}", n) }, SVal::Str(s) => format!("\"{}\"", s.replace('\\', "\\\\").replace('"', "\\\"").replace('\n', "\\n").replace('\r', "\\r").replace('\t', "\\t")), SVal::Arr(a) => format!("[{}]", a.borrow().iter().map(__st_js_raw).collect::<Vec<_>>().join(",")), SVal::Map(m) => format!("{{{}}}", m.borrow().iter().map(|(k, val)| format!("\"{}\": {}", k.replace('\\', "\\\\").replace('"', "\\\""), __st_js_raw(val)).replace("\": ", "\":")).collect::<Vec<_>>().join(",")), SVal::Func(_) => "null".into() } }
#[inline] pub fn __st_json_stringify(v: &SVal) -> SVal { SVal::Str(Cow::Owned(__st_js_raw(v))) }
fn __st_jp(cs: &[char], i: &mut usize) -> SVal { while *i < cs.len() && cs[*i].is_whitespace() { *i += 1; } if *i >= cs.len() { return SVal::Null; } match cs[*i] { '"' => { *i += 1; let mut s = String::new(); while *i < cs.len() { let c = cs[*i]; *i += 1; if c == '"' { break; } if c == '\\' && *i < cs.len() { let e = cs[*i]; *i += 1; match e { 'n' => s.push('\n'), 'r' => s.push('\r'), 't' => s.push('\t'), o => s.push(o) } } else { s.push(c); } } SVal::Str(Cow::Owned(s)) } '[' => { *i += 1; let mut a = Vec::new(); loop { while *i < cs.len() && cs[*i].is_whitespace() { *i += 1; } if *i >= cs.len() || cs[*i] == ']' { *i += 1; break; } a.push(__st_jp(cs, i)); while *i < cs.len() && cs[*i].is_whitespace() { *i += 1; } if *i < cs.len() && cs[*i] == ',' { *i += 1; } } SVal::Arr(Rc::new(RefCell::new(a))) } '{' => { *i += 1; let mut m = SMap::new(); loop { while *i < cs.len() && cs[*i].is_whitespace() { *i += 1; } if *i >= cs.len() || cs[*i] == '}' { *i += 1; break; } let k = __st_jp(cs, i).to_string(); while *i < cs.len() && (cs[*i].is_whitespace() || cs[*i] == ':') { *i += 1; } let v = __st_jp(cs, i); m.insert(Cow::Owned(k), v); while *i < cs.len() && cs[*i].is_whitespace() { *i += 1; } if *i < cs.len() && cs[*i] == ',' { *i += 1; } } SVal::Map(Rc::new(RefCell::new(m))) } 't' => { *i += 4; SVal::Bool(true) } 'f' => { *i += 5; SVal::Bool(false) } 'n' => { *i += 4; SVal::Null } _ => { let st = *i; while *i < cs.len() && (cs[*i].is_ascii_digit() || matches!(cs[*i], '-' | '+' | '.' | 'e' | 'E')) { *i += 1; } let ns: String = cs[st..*i].iter().collect(); SVal::Num(ns.parse().unwrap_or(0.0)) } } }
#[inline] pub fn __st_json_parse(v: &SVal) -> SVal { let s = match v { SVal::Str(st) => st.as_ref(), _ => "null" }; let cs: Vec<char> = s.chars().collect(); let mut i = 0; __st_jp(&cs, &mut i) }
#[inline] pub fn __st_args() -> SVal { let v: Vec<SVal> = std::env::args().skip(1).map(|a| SVal::Str(Cow::Owned(a))).collect(); SVal::Arr(Rc::new(RefCell::new(v))) }
#[inline] pub fn __st_mkdir(p: &SVal) -> SVal { SVal::Bool(std::fs::create_dir_all(p.to_string()).is_ok()) }
#[inline] pub fn __st_remove_dir(p: &SVal) -> SVal { SVal::Bool(std::fs::remove_dir_all(p.to_string()).is_ok()) }
#[inline] pub fn __st_rename(a: &SVal, b: &SVal) -> SVal { SVal::Bool(std::fs::rename(a.to_string(), b.to_string()).is_ok()) }
#[inline] pub fn __st_exec(cmd: &SVal, argv: Option<&SVal>) -> SVal { let c = cmd.to_string(); if let Some(SVal::Arr(a)) = argv { let as_: Vec<String> = a.borrow().iter().map(|x| x.to_string()).collect(); let out = std::process::Command::new(&c).args(&as_).output(); let mut m = SMap::new(); match out { Ok(o) => { m.insert(Cow::Borrowed("code"), SVal::Num(o.status.code().unwrap_or(-1) as f64)); m.insert(Cow::Borrowed("stderr"), SVal::Str(Cow::Owned(String::from_utf8_lossy(&o.stderr).to_string()))); m.insert(Cow::Borrowed("stdout"), SVal::Str(Cow::Owned(String::from_utf8_lossy(&o.stdout).to_string()))); } Err(e) => { m.insert(Cow::Borrowed("code"), SVal::Num(-1.0)); m.insert(Cow::Borrowed("stderr"), SVal::Str(Cow::Owned(e.to_string()))); m.insert(Cow::Borrowed("stdout"), SVal::Str(Cow::Borrowed(""))); } } return SVal::Map(Rc::new(RefCell::new(m))); } let out = if cfg!(windows) { std::process::Command::new("cmd").args(["/C", &c]).output() } else { std::process::Command::new("sh").args(["-c", &c]).output() }; SVal::Str(Cow::Owned(out.map(|o| String::from_utf8_lossy(&o.stdout).trim_end().to_string()).unwrap_or_default())) }
fn __st_sha256_raw(data: &[u8]) -> String { const K: [u32; 64] = [0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2]; let mut h: [u32; 8] = [0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19]; let bl = (data.len() as u64) * 8; let mut msg = data.to_vec(); msg.push(0x80); while (msg.len() % 64) != 56 { msg.push(0); } msg.extend_from_slice(&bl.to_be_bytes()); for ch in msg.chunks_exact(64) { let mut w = [0u32; 64]; for i in 0..16 { w[i] = u32::from_be_bytes([ch[4*i],ch[4*i+1],ch[4*i+2],ch[4*i+3]]); } for i in 16..64 { let s0 = w[i-15].rotate_right(7)^w[i-15].rotate_right(18)^(w[i-15]>>3); let s1 = w[i-2].rotate_right(17)^w[i-2].rotate_right(19)^(w[i-2]>>10); w[i] = w[i-16].wrapping_add(s0).wrapping_add(w[i-7]).wrapping_add(s1); } let [mut a,mut b,mut c,mut d,mut e,mut f,mut g,mut hh] = h; for i in 0..64 { let s1 = e.rotate_right(6)^e.rotate_right(11)^e.rotate_right(25); let chv = (e&f)^((!e)&g); let t1 = hh.wrapping_add(s1).wrapping_add(chv).wrapping_add(K[i]).wrapping_add(w[i]); let s0 = a.rotate_right(2)^a.rotate_right(13)^a.rotate_right(22); let maj = (a&b)^(a&c)^(b&c); let t2 = s0.wrapping_add(maj); hh=g; g=f; f=e; e=d.wrapping_add(t1); d=c; c=b; b=a; a=t1.wrapping_add(t2); } h[0]=h[0].wrapping_add(a); h[1]=h[1].wrapping_add(b); h[2]=h[2].wrapping_add(c); h[3]=h[3].wrapping_add(d); h[4]=h[4].wrapping_add(e); h[5]=h[5].wrapping_add(f); h[6]=h[6].wrapping_add(g); h[7]=h[7].wrapping_add(hh); } format!("{:08x}{:08x}{:08x}{:08x}{:08x}{:08x}{:08x}{:08x}",h[0],h[1],h[2],h[3],h[4],h[5],h[6],h[7]) }
#[inline] pub fn __st_sha256(v: &SVal) -> SVal { SVal::Str(Cow::Owned(__st_sha256_raw(v.to_string().as_bytes()))) }
#[inline] pub fn __st_sha256_file(p: &SVal) -> SVal { SVal::Str(Cow::Owned(std::fs::read(p.to_string()).map(|b| __st_sha256_raw(&b)).unwrap_or_default())) }
pub fn strip_v_prefix(mut raw: SVal) -> SVal {
    let mut s = __st_trim(&raw);
    if ((&(__st_starts_with(&s, &(SVal::Str(Cow::Borrowed("v")))))).is_truthy() || (&(__st_starts_with(&s, &(SVal::Str(Cow::Borrowed("V")))))).is_truthy()) {
        return __st_slice(&s, &(SVal::Num(1.000000)), Some(&(__st_len(&s))));
    }
    return s.clone();
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_strip_v_prefix(__a: Vec<SVal>) -> SVal { strip_v_prefix(__a.get(0).cloned().unwrap_or(SVal::Null)) }

pub fn is_all_digits(mut s: SVal) -> SVal {
    let char_code = SVal::Func(Rc::new(|args: Vec<SVal>| SVal::Num(args.get(0).map(|a| a.to_string().chars().next().map(|c| c as u32 as f64).unwrap_or(0.0)).unwrap_or(0.0))));
    if (&(__st_len(&s))).eq_num(0.000000) {
        return SVal::Bool(false);
    }
    let mut i = SVal::Num(0.000000);
    while ((&i).as_num() < (&(__st_len(&s))).as_num()) {
        let mut ch = __st_slice(&s, &i, Some(&((i.clone() + SVal::Num(1.000000)))));
        let mut code = (&char_code).call(vec![ch.clone()]);
        if (((&code).as_num() < (&(SVal::Num(48.000000))).as_num()) || ((&code).as_num() > (&(SVal::Num(57.000000))).as_num())) {
            return SVal::Bool(false);
        }
        i = (i.clone() + SVal::Num(1.000000));
    }
    return SVal::Bool(true);
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_is_all_digits(__a: Vec<SVal>) -> SVal { is_all_digits(__a.get(0).cloned().unwrap_or(SVal::Null)) }

pub fn parse_semver(mut raw: SVal) -> SVal {
    let min = SVal::Func(Rc::new(|args: Vec<SVal>| SVal::Num(args.get(0).map(|a| a.as_num()).unwrap_or(0.0).min(args.get(1).map(|b| b.as_num()).unwrap_or(0.0)))));
    let mut s = strip_v_prefix(raw.clone());
    let mut plus_idx = __st_index_of(&s, &(SVal::Str(Cow::Borrowed("+"))));
    let mut build = SVal::Str(Cow::Borrowed(""));
    if ((&plus_idx).as_num() >= (&(SVal::Num(0.000000))).as_num()) {
        build = __st_slice(&s, &((plus_idx.clone() + SVal::Num(1.000000))), Some(&(__st_len(&s))));
        s = __st_slice(&s, &(SVal::Num(0.000000)), Some(&plus_idx));
    }
    let mut dash_idx = __st_index_of(&s, &(SVal::Str(Cow::Borrowed("-"))));
    let mut pre = SVal::Str(Cow::Borrowed(""));
    if ((&dash_idx).as_num() >= (&(SVal::Num(0.000000))).as_num()) {
        pre = __st_slice(&s, &((dash_idx.clone() + SVal::Num(1.000000))), Some(&(__st_len(&s))));
        s = __st_slice(&s, &(SVal::Num(0.000000)), Some(&dash_idx));
    }
    let mut parts = __st_split(&s, &(SVal::Str(Cow::Borrowed("."))));
    if (((&(__st_len(&parts))).as_num() < (&(SVal::Num(2.000000))).as_num()) || ((&(__st_len(&parts))).as_num() > (&(SVal::Num(3.000000))).as_num())) {
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("valid"), SVal::Bool(false)); __m.insert(Cow::Borrowed("major"), SVal::Num(0.000000)); __m.insert(Cow::Borrowed("minor"), SVal::Num(0.000000)); __m.insert(Cow::Borrowed("patch"), SVal::Num(0.000000)); __m.insert(Cow::Borrowed("pre"), SVal::Str(Cow::Borrowed(""))); __m.insert(Cow::Borrowed("build"), SVal::Str(Cow::Borrowed(""))); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    let mut maj_s = (&parts).get_ref(&(SVal::Num(0.000000)));
    let mut min_s = (&parts).get_ref(&(SVal::Num(1.000000)));
    let mut pat_s = SVal::Str(Cow::Borrowed("0"));
    if (&(__st_len(&parts))).eq_num(3.000000) {
        pat_s = (&parts).get_ref(&(SVal::Num(2.000000)));
    }
    if (((!(&(is_all_digits(maj_s.clone()))).is_truthy()) || (!(&(is_all_digits(min_s.clone()))).is_truthy())) || (!(&(is_all_digits(pat_s.clone()))).is_truthy())) {
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("valid"), SVal::Bool(false)); __m.insert(Cow::Borrowed("major"), SVal::Num(0.000000)); __m.insert(Cow::Borrowed("minor"), SVal::Num(0.000000)); __m.insert(Cow::Borrowed("patch"), SVal::Num(0.000000)); __m.insert(Cow::Borrowed("pre"), SVal::Str(Cow::Borrowed(""))); __m.insert(Cow::Borrowed("build"), SVal::Str(Cow::Borrowed(""))); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("valid"), SVal::Bool(true)); __m.insert(Cow::Borrowed("major"), SVal::Num((&(SVal::Num((&maj_s).as_num()))).as_num().trunc())); __m.insert(Cow::Borrowed("minor"), SVal::Num((&(SVal::Num((&min_s).as_num()))).as_num().trunc())); __m.insert(Cow::Borrowed("patch"), SVal::Num((&(SVal::Num((&pat_s).as_num()))).as_num().trunc())); __m.insert(Cow::Borrowed("pre"), pre.clone()); __m.insert(Cow::Borrowed("build"), build.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_parse_semver(__a: Vec<SVal>) -> SVal { parse_semver(__a.get(0).cloned().unwrap_or(SVal::Null)) }

pub fn format_semver(mut v: SVal) -> SVal {
    let mut base = ((((__st_str(&((&v).get_key("major"))) + SVal::Str(Cow::Borrowed("."))) + __st_str(&((&v).get_key("minor")))) + SVal::Str(Cow::Borrowed("."))) + __st_str(&((&v).get_key("patch"))));
    if ((&(__st_len(&((&v).get_key("pre"))))).as_num() > (&(SVal::Num(0.000000))).as_num()) {
        base = ((base.clone() + SVal::Str(Cow::Borrowed("-"))) + (&v).get_key("pre"));
    }
    if ((&(__st_len(&((&v).get_key("build"))))).as_num() > (&(SVal::Num(0.000000))).as_num()) {
        base = ((base.clone() + SVal::Str(Cow::Borrowed("+"))) + (&v).get_key("build"));
    }
    return base.clone();
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_format_semver(__a: Vec<SVal>) -> SVal { format_semver(__a.get(0).cloned().unwrap_or(SVal::Null)) }

pub fn compare_prerelease(mut a: SVal, mut b: SVal) -> SVal {
    if (&a == &b) {
        return SVal::Num(0.000000);
    }
    if (&(__st_len(&a))).eq_num(0.000000) {
        return SVal::Num(1.000000);
    }
    if (&(__st_len(&b))).eq_num(0.000000) {
        return SVal::Num(-1.000000);
    }
    let mut pa = __st_split(&a, &(SVal::Str(Cow::Borrowed("."))));
    let mut pb = __st_split(&b, &(SVal::Str(Cow::Borrowed("."))));
    let mut limit = __st_len(&pa);
    if ((&(__st_len(&pb))).as_num() < (&limit).as_num()) {
        limit = __st_len(&pb);
    }
    let mut i = SVal::Num(0.000000);
    while ((&i).as_num() < (&limit).as_num()) {
        let mut xa = (&pa).get_ref(&i);
        let mut xb = (&pb).get_ref(&i);
        if (&xa != &xb) {
            let mut na = is_all_digits(xa.clone());
            let mut nb = is_all_digits(xb.clone());
            if ((&na).is_truthy() && (&nb).is_truthy()) {
                let mut va = SVal::Num((&xa).as_num());
                let mut vb = SVal::Num((&xb).as_num());
                if ((&va).as_num() < (&vb).as_num()) {
                    return SVal::Num(-1.000000);
                }
                if ((&va).as_num() > (&vb).as_num()) {
                    return SVal::Num(1.000000);
                }
            } else {
                if ((&na).is_truthy() && (!(&nb).is_truthy())) {
                    return SVal::Num(-1.000000);
                } else {
                    if ((!(&na).is_truthy()) && (&nb).is_truthy()) {
                        return SVal::Num(1.000000);
                    } else {
                        if ((&xa).as_num() < (&xb).as_num()) {
                            return SVal::Num(-1.000000);
                        }
                        return SVal::Num(1.000000);
                    }
                }
            }
        }
        i = (i.clone() + SVal::Num(1.000000));
    }
    if ((&(__st_len(&pa))).as_num() < (&(__st_len(&pb))).as_num()) {
        return SVal::Num(-1.000000);
    }
    if ((&(__st_len(&pa))).as_num() > (&(__st_len(&pb))).as_num()) {
        return SVal::Num(1.000000);
    }
    return SVal::Num(0.000000);
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_compare_prerelease(__a: Vec<SVal>) -> SVal { compare_prerelease(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null)) }

pub fn compare_semver(mut a_raw: SVal, mut b_raw: SVal) -> SVal {
    let mut a = parse_semver(a_raw.clone());
    let mut b = parse_semver(b_raw.clone());
    if (&((&a).get_key("major")) != &((&b).get_key("major"))) {
        if ((&((&a).get_key("major"))).as_num() < (&((&b).get_key("major"))).as_num()) {
            return SVal::Num(-1.000000);
        }
        return SVal::Num(1.000000);
    }
    if (&((&a).get_key("minor")) != &((&b).get_key("minor"))) {
        if ((&((&a).get_key("minor"))).as_num() < (&((&b).get_key("minor"))).as_num()) {
            return SVal::Num(-1.000000);
        }
        return SVal::Num(1.000000);
    }
    if (&((&a).get_key("patch")) != &((&b).get_key("patch"))) {
        if ((&((&a).get_key("patch"))).as_num() < (&((&b).get_key("patch"))).as_num()) {
            return SVal::Num(-1.000000);
        }
        return SVal::Num(1.000000);
    }
    return compare_prerelease((&a).get_key("pre"), (&b).get_key("pre"));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_compare_semver(__a: Vec<SVal>) -> SVal { compare_semver(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null)) }

pub fn satisfies_constraint(mut version_raw: SVal, mut constraint_raw: SVal) -> SVal {
    let mut c = __st_trim(&constraint_raw);
    if ((&(__st_len(&c))).eq_num(0.000000) || (&c).eq_str("*")) {
        return SVal::Bool(true);
    }
    let mut v = parse_semver(version_raw.clone());
    if (!(&((&v).get_key("valid"))).is_truthy()) {
        return SVal::Bool(false);
    }
    if (&(__st_starts_with(&c, &(SVal::Str(Cow::Borrowed(">=")))))).is_truthy() {
        return SVal::Bool(((&(compare_semver(version_raw.clone(), __st_trim(&(__st_slice(&c, &(SVal::Num(2.000000)), Some(&(__st_len(&c))))))))).as_num() >= (&(SVal::Num(0.000000))).as_num()));
    }
    if (&(__st_starts_with(&c, &(SVal::Str(Cow::Borrowed("<=")))))).is_truthy() {
        return SVal::Bool(((&(compare_semver(version_raw.clone(), __st_trim(&(__st_slice(&c, &(SVal::Num(2.000000)), Some(&(__st_len(&c))))))))).as_num() <= (&(SVal::Num(0.000000))).as_num()));
    }
    if (&(__st_starts_with(&c, &(SVal::Str(Cow::Borrowed(">")))))).is_truthy() {
        return SVal::Bool(((&(compare_semver(version_raw.clone(), __st_trim(&(__st_slice(&c, &(SVal::Num(1.000000)), Some(&(__st_len(&c))))))))).as_num() > (&(SVal::Num(0.000000))).as_num()));
    }
    if (&(__st_starts_with(&c, &(SVal::Str(Cow::Borrowed("<")))))).is_truthy() {
        return SVal::Bool(((&(compare_semver(version_raw.clone(), __st_trim(&(__st_slice(&c, &(SVal::Num(1.000000)), Some(&(__st_len(&c))))))))).as_num() < (&(SVal::Num(0.000000))).as_num()));
    }
    if (&(__st_starts_with(&c, &(SVal::Str(Cow::Borrowed("=")))))).is_truthy() {
        return SVal::Bool((&(compare_semver(version_raw.clone(), __st_trim(&(__st_slice(&c, &(SVal::Num(1.000000)), Some(&(__st_len(&c))))))))).eq_num(0.000000));
    }
    if (&(__st_starts_with(&c, &(SVal::Str(Cow::Borrowed("^")))))).is_truthy() {
        let mut base_s = __st_trim(&(__st_slice(&c, &(SVal::Num(1.000000)), Some(&(__st_len(&c))))));
        let mut base = parse_semver(base_s.clone());
        if (!(&((&base).get_key("valid"))).is_truthy()) {
            return SVal::Bool(false);
        }
        if ((&(compare_semver(version_raw.clone(), base_s.clone()))).as_num() < (&(SVal::Num(0.000000))).as_num()) {
            return SVal::Bool(false);
        }
        if ((&((&base).get_key("major"))).as_num() > (&(SVal::Num(0.000000))).as_num()) {
            return SVal::Bool((&((&v).get_key("major")) == &((&base).get_key("major"))));
        }
        if ((&((&base).get_key("minor"))).as_num() > (&(SVal::Num(0.000000))).as_num()) {
            return SVal::Bool(((&((&v).get_key("major"))).eq_num(0.000000) && (&((&v).get_key("minor")) == &((&base).get_key("minor")))));
        }
        return SVal::Bool((((&((&v).get_key("major"))).eq_num(0.000000) && (&((&v).get_key("minor"))).eq_num(0.000000)) && (&((&v).get_key("patch")) == &((&base).get_key("patch")))));
    }
    if (&(__st_starts_with(&c, &(SVal::Str(Cow::Borrowed("~")))))).is_truthy() {
        let mut base_s = __st_trim(&(__st_slice(&c, &(SVal::Num(1.000000)), Some(&(__st_len(&c))))));
        let mut base = parse_semver(base_s.clone());
        if (!(&((&base).get_key("valid"))).is_truthy()) {
            return SVal::Bool(false);
        }
        if ((&(compare_semver(version_raw.clone(), base_s.clone()))).as_num() < (&(SVal::Num(0.000000))).as_num()) {
            return SVal::Bool(false);
        }
        return SVal::Bool(((&((&v).get_key("major")) == &((&base).get_key("major"))) && (&((&v).get_key("minor")) == &((&base).get_key("minor")))));
    }
    return SVal::Bool(((&(compare_semver(version_raw.clone(), c.clone()))).as_num() >= (&(SVal::Num(0.000000))).as_num()));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_satisfies_constraint(__a: Vec<SVal>) -> SVal { satisfies_constraint(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null)) }

pub fn classify_transition(mut from_ver: SVal, mut to_ver: SVal) -> SVal {
    if ((&from_ver == &(SVal::Null)) || (&(__st_len(&(__st_str(&from_ver))))).eq_num(0.000000)) {
        return SVal::Str(Cow::Borrowed("install"));
    }
    let mut cmp = compare_semver(from_ver.clone(), to_ver.clone());
    if ((&cmp).as_num() < (&(SVal::Num(0.000000))).as_num()) {
        return SVal::Str(Cow::Borrowed("upgrade"));
    }
    if ((&cmp).as_num() > (&(SVal::Num(0.000000))).as_num()) {
        return SVal::Str(Cow::Borrowed("downgrade"));
    }
    return SVal::Str(Cow::Borrowed("noop"));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_classify_transition(__a: Vec<SVal>) -> SVal { classify_transition(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null)) }

pub fn is_protected_system_prefix(mut path: SVal) -> SVal {
    let mut blocked = SVal::Arr(Rc::new(RefCell::new(vec![SVal::Str(Cow::Borrowed("/etc")), SVal::Str(Cow::Borrowed("/boot")), SVal::Str(Cow::Borrowed("/proc")), SVal::Str(Cow::Borrowed("/sys")), SVal::Str(Cow::Borrowed("/dev")), SVal::Str(Cow::Borrowed("/bin")), SVal::Str(Cow::Borrowed("/sbin")), SVal::Str(Cow::Borrowed("/lib")), SVal::Str(Cow::Borrowed("/lib64")), SVal::Str(Cow::Borrowed("/root")), SVal::Str(Cow::Borrowed("/var/lib/dpkg")), SVal::Str(Cow::Borrowed("/var/lib/apt")), SVal::Str(Cow::Borrowed("/usr/bin/sudo"))])));
    let mut i = SVal::Num(0.000000);
    while ((&i).as_num() < (&(__st_len(&blocked))).as_num()) {
        let mut prefix = (&blocked).get_ref(&i);
        if ((&path == &prefix) || (&(__st_starts_with(&path, &((prefix.clone() + SVal::Str(Cow::Borrowed("/"))))))).is_truthy()) {
            return SVal::Bool(true);
        }
        i = (i.clone() + SVal::Num(1.000000));
    }
    return SVal::Bool(false);
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_is_protected_system_prefix(__a: Vec<SVal>) -> SVal { is_protected_system_prefix(__a.get(0).cloned().unwrap_or(SVal::Null)) }

pub fn validate_install_path(mut path: SVal, mut scope: SVal) -> SVal {
    if ((&path == &(SVal::Null)) || (&(__st_len(&path))).eq_num(0.000000)) {
        return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), SVal::Str(Cow::Borrowed("install path must not be empty")))]))));
    }
    if (&(__st_contains(&path, &(SVal::Str(Cow::Borrowed("//")))))).is_truthy() {
        return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), SVal::Str(Cow::Borrowed("duplicate slash in install path")))]))));
    }
    if (&(__st_contains(&path, &(SVal::Str(Cow::Borrowed("\\")))))).is_truthy() {
        return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), SVal::Str(Cow::Borrowed("backslash not allowed in unix install path")))]))));
    }
    let mut segs = __st_split(&path, &(SVal::Str(Cow::Borrowed("/"))));
    let mut i = SVal::Num(0.000000);
    while ((&i).as_num() < (&(__st_len(&segs))).as_num()) {
        if ((&((&segs).get_ref(&i))).eq_str("..") || (&((&segs).get_ref(&i))).eq_str(".")) {
            return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), SVal::Str(Cow::Borrowed("path traversal segment not allowed")))]))));
        }
        i = (i.clone() + SVal::Num(1.000000));
    }
    if (&(is_protected_system_prefix(path.clone()))).is_truthy() {
        return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), SVal::Str(Cow::Borrowed("protected system or dpkg directory")))]))));
    }
    if (&scope).eq_str("system") {
        if ((!(&(__st_starts_with(&path, &(SVal::Str(Cow::Borrowed("/usr/share/")))))).is_truthy()) && (!(&(__st_starts_with(&path, &(SVal::Str(Cow::Borrowed("/opt/infinityos/")))))).is_truthy())) {
            return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), SVal::Str(Cow::Borrowed("system scope must target /usr/share/ or /opt/infinityos/")))]))));
        }
        if ((&(__st_len(&segs))).as_num() < (&(SVal::Num(4.000000))).as_num()) {
            return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), SVal::Str(Cow::Borrowed("system install path is too shallow")))]))));
        }
        return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(true)), (Cow::Borrowed("reason"), SVal::Str(Cow::Borrowed("")))]))));
    }
    if (&scope).eq_str("user") {
        if ((!(&(__st_starts_with(&path, &(SVal::Str(Cow::Borrowed("~/.local/share/")))))).is_truthy()) && (!(&(__st_starts_with(&path, &(SVal::Str(Cow::Borrowed("/home/")))))).is_truthy())) {
            return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), SVal::Str(Cow::Borrowed("user scope must target ~/.local/share/ or /home/")))]))));
        }
        return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(true)), (Cow::Borrowed("reason"), SVal::Str(Cow::Borrowed("")))]))));
    }
    return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), (SVal::Str(Cow::Borrowed("unknown install scope: ")) + __st_str(&scope)))]))));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_validate_install_path(__a: Vec<SVal>) -> SVal { validate_install_path(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null)) }

pub fn validate_archive_entry(mut entry_path: SVal) -> SVal {
    if ((&entry_path == &(SVal::Null)) || (&(__st_len(&entry_path))).eq_num(0.000000)) {
        return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), SVal::Str(Cow::Borrowed("empty archive entry")))]))));
    }
    if (&(__st_starts_with(&entry_path, &(SVal::Str(Cow::Borrowed("/")))))).is_truthy() {
        return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), (SVal::Str(Cow::Borrowed("absolute path in archive: ")) + entry_path.clone()))]))));
    }
    if (&(__st_contains(&entry_path, &(SVal::Str(Cow::Borrowed("\\")))))).is_truthy() {
        return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), (SVal::Str(Cow::Borrowed("backslash in archive entry: ")) + entry_path.clone()))]))));
    }
    let mut segs = __st_split(&entry_path, &(SVal::Str(Cow::Borrowed("/"))));
    let mut i = SVal::Num(0.000000);
    while ((&i).as_num() < (&(__st_len(&segs))).as_num()) {
        if (&((&segs).get_ref(&i))).eq_str("..") {
            return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), (SVal::Str(Cow::Borrowed("zip-slip traversal in archive: ")) + entry_path.clone()))]))));
        }
        i = (i.clone() + SVal::Num(1.000000));
    }
    return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(true)), (Cow::Borrowed("reason"), SVal::Str(Cow::Borrowed("")))]))));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_validate_archive_entry(__a: Vec<SVal>) -> SVal { validate_archive_entry(__a.get(0).cloned().unwrap_or(SVal::Null)) }

pub fn validate_archive_manifest(mut entries: SVal) -> SVal {
    let mut i = SVal::Num(0.000000);
    while ((&i).as_num() < (&(__st_len(&entries))).as_num()) {
        let mut chk = validate_archive_entry((&entries).get_ref(&i));
        if (!(&((&chk).get_key("ok"))).is_truthy()) {
            return chk.clone();
        }
        i = (i.clone() + SVal::Num(1.000000));
    }
    return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(true)), (Cow::Borrowed("reason"), SVal::Str(Cow::Borrowed("")))]))));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_validate_archive_manifest(__a: Vec<SVal>) -> SVal { validate_archive_manifest(__a.get(0).cloned().unwrap_or(SVal::Null)) }

pub fn requires_privilege_escalation(mut scope: SVal) -> SVal {
    return SVal::Bool((&scope).eq_str("system"));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_requires_privilege_escalation(__a: Vec<SVal>) -> SVal { requires_privilege_escalation(__a.get(0).cloned().unwrap_or(SVal::Null)) }

pub fn is_valid_hex_string(mut s: SVal, mut expected_len: SVal) -> SVal {
    let char_code = SVal::Func(Rc::new(|args: Vec<SVal>| SVal::Num(args.get(0).map(|a| a.to_string().chars().next().map(|c| c as u32 as f64).unwrap_or(0.0)).unwrap_or(0.0))));
    let exp = SVal::Func(Rc::new(|args: Vec<SVal>| SVal::Num(args.first().map(|a| a.as_num().exp()).unwrap_or(0.0))));
    if ((&s == &(SVal::Null)) || (&(__st_len(&s)) != &expected_len)) {
        return SVal::Bool(false);
    }
    let mut low = __st_lower(&s);
    let mut i = SVal::Num(0.000000);
    while ((&i).as_num() < (&(__st_len(&low))).as_num()) {
        let mut ch = __st_slice(&low, &i, Some(&((i.clone() + SVal::Num(1.000000)))));
        let mut code = (&char_code).call(vec![ch.clone()]);
        let mut is_digit = SVal::Bool((((&code).as_num() >= (&(SVal::Num(48.000000))).as_num()) && ((&code).as_num() <= (&(SVal::Num(57.000000))).as_num())));
        let mut is_hex = SVal::Bool((((&code).as_num() >= (&(SVal::Num(97.000000))).as_num()) && ((&code).as_num() <= (&(SVal::Num(102.000000))).as_num())));
        if ((!(&is_digit).is_truthy()) && (!(&is_hex).is_truthy())) {
            return SVal::Bool(false);
        }
        i = (i.clone() + SVal::Num(1.000000));
    }
    return SVal::Bool(true);
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_is_valid_hex_string(__a: Vec<SVal>) -> SVal { is_valid_hex_string(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null)) }

pub fn validate_sha256(mut digest: SVal) -> SVal {
    return is_valid_hex_string(digest.clone(), SVal::Num(64.000000));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_validate_sha256(__a: Vec<SVal>) -> SVal { validate_sha256(__a.get(0).cloned().unwrap_or(SVal::Null)) }

pub fn validate_ed25519_sig(mut sig: SVal) -> SVal {
    return is_valid_hex_string(sig.clone(), SVal::Num(128.000000));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_validate_ed25519_sig(__a: Vec<SVal>) -> SVal { validate_ed25519_sig(__a.get(0).cloned().unwrap_or(SVal::Null)) }

pub fn verify_component_integrity(mut entry: SVal, mut computed_sha256: SVal, mut sig_verified: SVal) -> SVal {
    let exp = SVal::Func(Rc::new(|args: Vec<SVal>| SVal::Num(args.first().map(|a| a.as_num().exp()).unwrap_or(0.0))));
    let mut expected = (&entry).get_key("sha256");
    if ((&expected != &(SVal::Null)) && ((&(__st_len(&(__st_str(&expected))))).as_num() > (&(SVal::Num(0.000000))).as_num())) {
        if (!(&(validate_sha256(__st_str(&expected)))).is_truthy()) {
            return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), SVal::Str(Cow::Borrowed("malformed expected sha256 digest")))]))));
        }
        if (&(__st_lower(&(__st_str(&computed_sha256)))) != &(__st_lower(&(__st_str(&expected))))) {
            return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), (((SVal::Str(Cow::Borrowed("sha256 mismatch: expected ")) + __st_str(&expected)) + SVal::Str(Cow::Borrowed(", got "))) + __st_str(&computed_sha256)))]))));
        }
    }
    if ((&(__st_has_key(&entry, &(SVal::Str(Cow::Borrowed("signature")))))).is_truthy() && ((&(__st_len(&(__st_str(&((&entry).get_key("signature"))))))).as_num() > (&(SVal::Num(0.000000))).as_num())) {
        if (!(&sig_verified).is_truthy()) {
            return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), SVal::Str(Cow::Borrowed("ed25519 signature verification failed")))]))));
        }
    }
    return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(true)), (Cow::Borrowed("reason"), SVal::Str(Cow::Borrowed("")))]))));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_verify_component_integrity(__a: Vec<SVal>) -> SVal { verify_component_integrity(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null), __a.get(2).cloned().unwrap_or(SVal::Null)) }

pub fn is_valid_component_id(mut id: SVal) -> SVal {
    let char_code = SVal::Func(Rc::new(|args: Vec<SVal>| SVal::Num(args.get(0).map(|a| a.to_string().chars().next().map(|c| c as u32 as f64).unwrap_or(0.0)).unwrap_or(0.0))));
    if (((&id == &(SVal::Null)) || ((&(__st_len(&id))).as_num() < (&(SVal::Num(2.000000))).as_num())) || ((&(__st_len(&id))).as_num() > (&(SVal::Num(64.000000))).as_num())) {
        return SVal::Bool(false);
    }
    let mut i = SVal::Num(0.000000);
    while ((&i).as_num() < (&(__st_len(&id))).as_num()) {
        let mut c = (&char_code).call(vec![__st_slice(&id, &i, Some(&((i.clone() + SVal::Num(1.000000)))))]);
        let mut ok_low = SVal::Bool((((&c).as_num() >= (&(SVal::Num(97.000000))).as_num()) && ((&c).as_num() <= (&(SVal::Num(122.000000))).as_num())));
        let mut ok_dig = SVal::Bool((((&c).as_num() >= (&(SVal::Num(48.000000))).as_num()) && ((&c).as_num() <= (&(SVal::Num(57.000000))).as_num())));
        let mut ok_sym = SVal::Bool((((&c).eq_num(45.000000) || (&c).eq_num(95.000000)) || (&c).eq_num(46.000000)));
        if (((!(&ok_low).is_truthy()) && (!(&ok_dig).is_truthy())) && (!(&ok_sym).is_truthy())) {
            return SVal::Bool(false);
        }
        i = (i.clone() + SVal::Num(1.000000));
    }
    return SVal::Bool(true);
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_is_valid_component_id(__a: Vec<SVal>) -> SVal { is_valid_component_id(__a.get(0).cloned().unwrap_or(SVal::Null)) }

pub fn validate_catalogue_entry(mut id: SVal, mut entry: SVal) -> SVal {
    if (!(&(is_valid_component_id(id.clone()))).is_truthy()) {
        return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), (SVal::Str(Cow::Borrowed("invalid component id: ")) + __st_str(&id)))]))));
    }
    if (!(&(__st_has_key(&entry, &(SVal::Str(Cow::Borrowed("version")))))).is_truthy()) {
        return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), (id.clone() + SVal::Str(Cow::Borrowed(": missing version"))))]))));
    }
    let mut v = parse_semver((&entry).get_key("version"));
    if (!(&((&v).get_key("valid"))).is_truthy()) {
        return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), ((id.clone() + SVal::Str(Cow::Borrowed(": invalid semver: "))) + __st_str(&((&entry).get_key("version")))))]))));
    }
    if (!(&(__st_has_key(&entry, &(SVal::Str(Cow::Borrowed("install_path")))))).is_truthy()) {
        return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), (id.clone() + SVal::Str(Cow::Borrowed(": missing install_path"))))]))));
    }
    let mut scope = SVal::Str(Cow::Borrowed("system"));
    if (((&(__st_has_key(&entry, &(SVal::Str(Cow::Borrowed("scope")))))).is_truthy() && (&((&entry).get_key("scope")) != &(SVal::Null))) && ((&(__st_len(&(__st_str(&((&entry).get_key("scope"))))))).as_num() > (&(SVal::Num(0.000000))).as_num())) {
        scope = (&entry).get_key("scope");
    }
    let mut pchk = validate_install_path((&entry).get_key("install_path"), scope.clone());
    if (!(&((&pchk).get_key("ok"))).is_truthy()) {
        return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), ((id.clone() + SVal::Str(Cow::Borrowed(": "))) + (&pchk).get_key("reason")))]))));
    }
    if (((&(__st_has_key(&entry, &(SVal::Str(Cow::Borrowed("sha256")))))).is_truthy() && (&((&entry).get_key("sha256")) != &(SVal::Null))) && ((&(__st_len(&(__st_str(&((&entry).get_key("sha256"))))))).as_num() > (&(SVal::Num(0.000000))).as_num())) {
        if (!(&(validate_sha256(__st_str(&((&entry).get_key("sha256")))))).is_truthy()) {
            return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), (id.clone() + SVal::Str(Cow::Borrowed(": invalid sha256 hex"))))]))));
        }
    }
    return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(true)), (Cow::Borrowed("reason"), SVal::Str(Cow::Borrowed("")))]))));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_validate_catalogue_entry(__a: Vec<SVal>) -> SVal { validate_catalogue_entry(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null)) }

pub fn validate_catalogue(mut catalogue: SVal) -> SVal {
    let keys = SVal::Func(Rc::new(|args: Vec<SVal>| { if let Some(SVal::Map(m)) = args.get(0) { let ks = m.borrow().keys().map(|k| SVal::Str(k.clone())).collect(); return SVal::Arr(Rc::new(RefCell::new(ks))); } SVal::Null }));
    let mut ks = (&keys).call(vec![catalogue.clone()]);
    let mut errors = SVal::Arr(Rc::new(RefCell::new(vec![])));
    let mut i = SVal::Num(0.000000);
    while ((&i).as_num() < (&(__st_len(&ks))).as_num()) {
        let mut id = (&ks).get_ref(&i);
        let mut chk = validate_catalogue_entry(id.clone(), (&catalogue).get_ref(&id));
        if (!(&((&chk).get_key("ok"))).is_truthy()) {
            __st_push(&errors, (&chk).get_key("reason"));
        }
        i = (i.clone() + SVal::Num(1.000000));
    }
    return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool((&(__st_len(&errors))).eq_num(0.000000))); __m.insert(Cow::Borrowed("count"), __st_len(&ks)); __m.insert(Cow::Borrowed("errors"), errors.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_validate_catalogue(__a: Vec<SVal>) -> SVal { validate_catalogue(__a.get(0).cloned().unwrap_or(SVal::Null)) }

pub fn matches_channel(mut entry: SVal, mut channel: SVal) -> SVal {
    if (((&channel == &(SVal::Null)) || (&(__st_len(&channel))).eq_num(0.000000)) || (&channel).eq_str("all")) {
        return SVal::Bool(true);
    }
    if (((!(&(__st_has_key(&entry, &(SVal::Str(Cow::Borrowed("channel")))))).is_truthy()) || (&((&entry).get_key("channel")) == &(SVal::Null))) || (&(__st_len(&(__st_str(&((&entry).get_key("channel"))))))).eq_num(0.000000)) {
        return SVal::Bool((&channel).eq_str("stable"));
    }
    let mut ec = (&entry).get_key("channel");
    if (&channel).eq_str("nightly") {
        return SVal::Bool(true);
    }
    if (&channel).eq_str("beta") {
        return SVal::Bool(((&ec).eq_str("stable") || (&ec).eq_str("beta")));
    }
    return SVal::Bool((&ec).eq_str("stable"));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_matches_channel(__a: Vec<SVal>) -> SVal { matches_channel(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null)) }

pub fn search_catalogue(mut catalogue: SVal, mut query: SVal, mut channel: SVal) -> SVal {
    let keys = SVal::Func(Rc::new(|args: Vec<SVal>| { if let Some(SVal::Map(m)) = args.get(0) { let ks = m.borrow().keys().map(|k| SVal::Str(k.clone())).collect(); return SVal::Arr(Rc::new(RefCell::new(ks))); } SVal::Null }));
    let mut q = __st_lower(&(__st_trim(&query)));
    let mut ks = __st_sort(&((&keys).call(vec![catalogue.clone()])));
    let mut matches = SVal::Arr(Rc::new(RefCell::new(vec![])));
    let mut i = SVal::Num(0.000000);
    while ((&i).as_num() < (&(__st_len(&ks))).as_num()) {
        let mut id = (&ks).get_ref(&i);
        let mut entry = (&catalogue).get_ref(&id);
        if (&(matches_channel(entry.clone(), channel.clone()))).is_truthy() {
            let mut desc = SVal::Str(Cow::Borrowed(""));
            if ((&(__st_has_key(&entry, &(SVal::Str(Cow::Borrowed("description")))))).is_truthy() && (&((&entry).get_key("description")) != &(SVal::Null))) {
                desc = __st_lower(&(__st_str(&((&entry).get_key("description")))));
            }
            let mut cat = SVal::Str(Cow::Borrowed(""));
            if ((&(__st_has_key(&entry, &(SVal::Str(Cow::Borrowed("category")))))).is_truthy() && (&((&entry).get_key("category")) != &(SVal::Null))) {
                cat = __st_lower(&(__st_str(&((&entry).get_key("category")))));
            }
            if ((((&(__st_len(&q))).eq_num(0.000000) || (&(__st_contains(&(__st_lower(&id)), &q))).is_truthy()) || (&(__st_contains(&desc, &q))).is_truthy()) || (&(__st_contains(&cat, &q))).is_truthy()) {
                __st_push(&matches, { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("id"), id.clone()); __m.insert(Cow::Borrowed("version"), (&entry).get_key("version")); __m.insert(Cow::Borrowed("scope"), (if (&(__st_has_key(&entry, &(SVal::Str(Cow::Borrowed("scope")))))).is_truthy() { (&entry).get_key("scope") } else { SVal::Str(Cow::Borrowed("system")) })); __m.insert(Cow::Borrowed("description"), (if (&(__st_has_key(&entry, &(SVal::Str(Cow::Borrowed("description")))))).is_truthy() { (&entry).get_key("description") } else { SVal::Str(Cow::Borrowed("")) })); __m.insert(Cow::Borrowed("category"), (if (&(__st_has_key(&entry, &(SVal::Str(Cow::Borrowed("category")))))).is_truthy() { (&entry).get_key("category") } else { SVal::Str(Cow::Borrowed("general")) })); SVal::Map(Rc::new(RefCell::new(__m))) });
            }
        }
        i = (i.clone() + SVal::Num(1.000000));
    }
    return matches.clone();
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_search_catalogue(__a: Vec<SVal>) -> SVal { search_catalogue(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null), __a.get(2).cloned().unwrap_or(SVal::Null)) }

pub fn dfs_visit(mut id: SVal, mut catalogue: SVal, mut visiting: SVal, mut visited: SVal, mut order: SVal, mut err_box: SVal) -> SVal {
    if ((&(__st_len(&((&err_box).get_key("msg"))))).as_num() > (&(SVal::Num(0.000000))).as_num()) {
        return SVal::Null;
    }
    if (&(__st_has_key(&visited, &id))).is_truthy() {
        return SVal::Null;
    }
    if (&(__st_has_key(&visiting, &id))).is_truthy() {
        (&err_box).set_key("msg", (SVal::Str(Cow::Borrowed("dependency cycle detected at ")) + id.clone()));
        return SVal::Null;
    }
    if (!(&(__st_has_key(&catalogue, &id))).is_truthy()) {
        (&err_box).set_key("msg", (SVal::Str(Cow::Borrowed("unknown component: ")) + id.clone()));
        return SVal::Null;
    }
    (&visiting).set(id.clone(), SVal::Bool(true));
    let mut entry = (&catalogue).get_ref(&id);
    if ((&(__st_has_key(&entry, &(SVal::Str(Cow::Borrowed("depends")))))).is_truthy() && (&((&entry).get_key("depends")) != &(SVal::Null))) {
        let mut deps = (&entry).get_key("depends");
        let mut i = SVal::Num(0.000000);
        while ((&i).as_num() < (&(__st_len(&deps))).as_num()) {
            let mut dep = (&deps).get_ref(&i);
            let mut dep_id = (&dep).get_key("id");
            if (!(&(__st_has_key(&catalogue, &dep_id))).is_truthy()) {
                (&err_box).set_key("msg", ((id.clone() + SVal::Str(Cow::Borrowed(" depends on missing component: "))) + dep_id.clone()));
                return SVal::Null;
            }
            if (((&(__st_has_key(&dep, &(SVal::Str(Cow::Borrowed("min_version")))))).is_truthy() && (&((&dep).get_key("min_version")) != &(SVal::Null))) && ((&(__st_len(&(__st_str(&((&dep).get_key("min_version"))))))).as_num() > (&(SVal::Num(0.000000))).as_num())) {
                let mut avail = (&((&catalogue).get_ref(&dep_id))).get_key("version");
                if (!(&(satisfies_constraint(avail.clone(), __st_str(&((&dep).get_key("min_version")))))).is_truthy()) {
                    (&err_box).set_key("msg", ((((((id.clone() + SVal::Str(Cow::Borrowed(" requires "))) + dep_id.clone()) + SVal::Str(Cow::Borrowed(" "))) + __st_str(&((&dep).get_key("min_version")))) + SVal::Str(Cow::Borrowed(" but catalogue has "))) + __st_str(&avail)));
                    return SVal::Null;
                }
            }
            dfs_visit(dep_id.clone(), catalogue.clone(), visiting.clone(), visited.clone(), order.clone(), err_box.clone());
            if ((&(__st_len(&((&err_box).get_key("msg"))))).as_num() > (&(SVal::Num(0.000000))).as_num()) {
                return SVal::Null;
            }
            i = (i.clone() + SVal::Num(1.000000));
        }
    }
    __st_delete_key(&visiting, &id);
    (&visited).set(id.clone(), SVal::Bool(true));
    __st_push(&order, id.clone());
    return SVal::Null;
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_dfs_visit(__a: Vec<SVal>) -> SVal { dfs_visit(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null), __a.get(2).cloned().unwrap_or(SVal::Null), __a.get(3).cloned().unwrap_or(SVal::Null), __a.get(4).cloned().unwrap_or(SVal::Null), __a.get(5).cloned().unwrap_or(SVal::Null)) }

pub fn resolve_install_order(mut catalogue: SVal, mut targets: SVal) -> SVal {
    let mut visiting = SVal::Map(Rc::new(RefCell::new(SMap::new())));
    let mut visited = SVal::Map(Rc::new(RefCell::new(SMap::new())));
    let mut order = SVal::Arr(Rc::new(RefCell::new(vec![])));
    let mut err_box = SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("msg"), SVal::Str(Cow::Borrowed("")))]))));
    let mut i = SVal::Num(0.000000);
    while ((&i).as_num() < (&(__st_len(&targets))).as_num()) {
        dfs_visit((&targets).get_ref(&i), catalogue.clone(), visiting.clone(), visited.clone(), order.clone(), err_box.clone());
        if ((&(__st_len(&((&err_box).get_key("msg"))))).as_num() > (&(SVal::Num(0.000000))).as_num()) {
            return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(false)); __m.insert(Cow::Borrowed("order"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("error"), (&err_box).get_key("msg")); SVal::Map(Rc::new(RefCell::new(__m))) };
        }
        i = (i.clone() + SVal::Num(1.000000));
    }
    return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(true)); __m.insert(Cow::Borrowed("order"), order.clone()); __m.insert(Cow::Borrowed("error"), SVal::Str(Cow::Borrowed(""))); SVal::Map(Rc::new(RefCell::new(__m))) };
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_resolve_install_order(__a: Vec<SVal>) -> SVal { resolve_install_order(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null)) }

pub fn find_reverse_dependents(mut catalogue: SVal, mut installed: SVal, mut target_id: SVal) -> SVal {
    let keys = SVal::Func(Rc::new(|args: Vec<SVal>| { if let Some(SVal::Map(m)) = args.get(0) { let ks = m.borrow().keys().map(|k| SVal::Str(k.clone())).collect(); return SVal::Arr(Rc::new(RefCell::new(ks))); } SVal::Null }));
    let mut dependents = SVal::Arr(Rc::new(RefCell::new(vec![])));
    let mut ks = __st_sort(&((&keys).call(vec![installed.clone()])));
    let mut i = SVal::Num(0.000000);
    while ((&i).as_num() < (&(__st_len(&ks))).as_num()) {
        let mut id = (&ks).get_ref(&i);
        if ((&id != &target_id) && (&(__st_has_key(&catalogue, &id))).is_truthy()) {
            let mut entry = (&catalogue).get_ref(&id);
            if ((&(__st_has_key(&entry, &(SVal::Str(Cow::Borrowed("depends")))))).is_truthy() && (&((&entry).get_key("depends")) != &(SVal::Null))) {
                let mut deps = (&entry).get_key("depends");
                let mut j = SVal::Num(0.000000);
                while ((&j).as_num() < (&(__st_len(&deps))).as_num()) {
                    if (&((&((&deps).get_ref(&j))).get_key("id")) == &target_id) {
                        __st_push(&dependents, id.clone());
                    }
                    j = (j.clone() + SVal::Num(1.000000));
                }
            }
        }
        i = (i.clone() + SVal::Num(1.000000));
    }
    return dependents.clone();
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_find_reverse_dependents(__a: Vec<SVal>) -> SVal { find_reverse_dependents(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null), __a.get(2).cloned().unwrap_or(SVal::Null)) }

pub fn check_file_conflicts(mut ownership: SVal, mut component_id: SVal, mut files: SVal) -> SVal {
    if ((&ownership == &(SVal::Null)) || (&files == &(SVal::Null))) {
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(true)); __m.insert(Cow::Borrowed("conflict"), SVal::Str(Cow::Borrowed(""))); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    let mut i = SVal::Num(0.000000);
    while ((&i).as_num() < (&(__st_len(&files))).as_num()) {
        let mut f = (&files).get_ref(&i);
        if ((&(__st_has_key(&ownership, &f))).is_truthy() && (&((&ownership).get_ref(&f)) != &component_id)) {
            return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(false)); __m.insert(Cow::Borrowed("conflict"), (((SVal::Str(Cow::Borrowed("file ")) + f.clone()) + SVal::Str(Cow::Borrowed(" is already owned by "))) + __st_str(&((&ownership).get_ref(&f))))); SVal::Map(Rc::new(RefCell::new(__m))) };
        }
        i = (i.clone() + SVal::Num(1.000000));
    }
    return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(true)); __m.insert(Cow::Borrowed("conflict"), SVal::Str(Cow::Borrowed(""))); SVal::Map(Rc::new(RefCell::new(__m))) };
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_check_file_conflicts(__a: Vec<SVal>) -> SVal { check_file_conflicts(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null), __a.get(2).cloned().unwrap_or(SVal::Null)) }

pub fn plan_reconcile(mut installed: SVal, mut catalogue: SVal, mut targets: SVal) -> SVal {
    let mut resolved = resolve_install_order(catalogue.clone(), targets.clone());
    if (!(&((&resolved).get_key("ok"))).is_truthy()) {
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(false)); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("skipped"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("requires_root"), SVal::Bool(false)); __m.insert(Cow::Borrowed("error"), (&resolved).get_key("error")); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    let mut actions = SVal::Arr(Rc::new(RefCell::new(vec![])));
    let mut skipped = SVal::Arr(Rc::new(RefCell::new(vec![])));
    let mut needs_root = SVal::Bool(false);
    let mut order = (&resolved).get_key("order");
    let mut i = SVal::Num(0.000000);
    while ((&i).as_num() < (&(__st_len(&order))).as_num()) {
        let mut id = (&order).get_ref(&i);
        let mut entry = (&catalogue).get_ref(&id);
        let mut scope = SVal::Str(Cow::Borrowed("system"));
        if (((&(__st_has_key(&entry, &(SVal::Str(Cow::Borrowed("scope")))))).is_truthy() && (&((&entry).get_key("scope")) != &(SVal::Null))) && ((&(__st_len(&(__st_str(&((&entry).get_key("scope"))))))).as_num() > (&(SVal::Num(0.000000))).as_num())) {
            scope = (&entry).get_key("scope");
        }
        let mut pchk = validate_install_path((&entry).get_key("install_path"), scope.clone());
        if (!(&((&pchk).get_key("ok"))).is_truthy()) {
            return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(false)); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("skipped"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("requires_root"), SVal::Bool(false)); __m.insert(Cow::Borrowed("error"), ((id.clone() + SVal::Str(Cow::Borrowed(": "))) + (&pchk).get_key("reason"))); SVal::Map(Rc::new(RefCell::new(__m))) };
        }
        let mut cur = SVal::Str(Cow::Borrowed(""));
        if ((&(__st_has_key(&installed, &id))).is_truthy() && (&((&installed).get_ref(&id)) != &(SVal::Null))) {
            cur = __st_str(&((&installed).get_ref(&id)));
        }
        let mut target_ver = __st_str(&((&entry).get_key("version")));
        let mut kind = classify_transition(cur.clone(), target_ver.clone());
        if (&kind).eq_str("noop") {
            __st_push(&skipped, id.clone());
        } else {
            if (&(requires_privilege_escalation(scope.clone()))).is_truthy() {
                needs_root = SVal::Bool(true);
            }
            __st_push(&actions, { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("id"), id.clone()); __m.insert(Cow::Borrowed("action"), kind.clone()); __m.insert(Cow::Borrowed("from"), cur.clone()); __m.insert(Cow::Borrowed("to"), target_ver.clone()); __m.insert(Cow::Borrowed("scope"), scope.clone()); __m.insert(Cow::Borrowed("install_path"), (&entry).get_key("install_path")); __m.insert(Cow::Borrowed("url"), (if (&(__st_has_key(&entry, &(SVal::Str(Cow::Borrowed("url")))))).is_truthy() { (&entry).get_key("url") } else { SVal::Str(Cow::Borrowed("")) })); __m.insert(Cow::Borrowed("sha256"), (if (&(__st_has_key(&entry, &(SVal::Str(Cow::Borrowed("sha256")))))).is_truthy() { (&entry).get_key("sha256") } else { SVal::Str(Cow::Borrowed("")) })); SVal::Map(Rc::new(RefCell::new(__m))) });
        }
        i = (i.clone() + SVal::Num(1.000000));
    }
    return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(true)); __m.insert(Cow::Borrowed("actions"), actions.clone()); __m.insert(Cow::Borrowed("skipped"), skipped.clone()); __m.insert(Cow::Borrowed("requires_root"), needs_root.clone()); __m.insert(Cow::Borrowed("error"), SVal::Str(Cow::Borrowed(""))); SVal::Map(Rc::new(RefCell::new(__m))) };
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_plan_reconcile(__a: Vec<SVal>) -> SVal { plan_reconcile(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null), __a.get(2).cloned().unwrap_or(SVal::Null)) }

pub fn plan_check_updates(mut installed: SVal, mut catalogue: SVal, mut channel: SVal) -> SVal {
    let keys = SVal::Func(Rc::new(|args: Vec<SVal>| { if let Some(SVal::Map(m)) = args.get(0) { let ks = m.borrow().keys().map(|k| SVal::Str(k.clone())).collect(); return SVal::Arr(Rc::new(RefCell::new(ks))); } SVal::Null }));
    let mut updates = SVal::Arr(Rc::new(RefCell::new(vec![])));
    let mut up_to_date = SVal::Arr(Rc::new(RefCell::new(vec![])));
    let mut ks = __st_sort(&((&keys).call(vec![installed.clone()])));
    let mut i = SVal::Num(0.000000);
    while ((&i).as_num() < (&(__st_len(&ks))).as_num()) {
        let mut id = (&ks).get_ref(&i);
        if (&(__st_has_key(&catalogue, &id))).is_truthy() {
            let mut entry = (&catalogue).get_ref(&id);
            if (&(matches_channel(entry.clone(), channel.clone()))).is_truthy() {
                let mut cur = __st_str(&((&installed).get_ref(&id)));
                let mut latest = __st_str(&((&entry).get_key("version")));
                if ((&(compare_semver(cur.clone(), latest.clone()))).as_num() < (&(SVal::Num(0.000000))).as_num()) {
                    __st_push(&updates, { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("id"), id.clone()); __m.insert(Cow::Borrowed("from"), cur.clone()); __m.insert(Cow::Borrowed("to"), latest.clone()); __m.insert(Cow::Borrowed("scope"), (if (&(__st_has_key(&entry, &(SVal::Str(Cow::Borrowed("scope")))))).is_truthy() { (&entry).get_key("scope") } else { SVal::Str(Cow::Borrowed("system")) })); SVal::Map(Rc::new(RefCell::new(__m))) });
                } else {
                    __st_push(&up_to_date, id.clone());
                }
            }
        }
        i = (i.clone() + SVal::Num(1.000000));
    }
    return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(true)); __m.insert(Cow::Borrowed("updates"), updates.clone()); __m.insert(Cow::Borrowed("up_to_date"), up_to_date.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_plan_check_updates(__a: Vec<SVal>) -> SVal { plan_check_updates(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null), __a.get(2).cloned().unwrap_or(SVal::Null)) }

pub fn plan_remove(mut installed: SVal, mut catalogue: SVal, mut target_id: SVal, mut force: SVal) -> SVal {
    if (!(&(__st_has_key(&installed, &target_id))).is_truthy()) {
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(false)); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("blocked_by"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("requires_root"), SVal::Bool(false)); __m.insert(Cow::Borrowed("error"), (SVal::Str(Cow::Borrowed("component is not installed: ")) + target_id.clone())); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    let mut deps = find_reverse_dependents(catalogue.clone(), installed.clone(), target_id.clone());
    if (((&(__st_len(&deps))).as_num() > (&(SVal::Num(0.000000))).as_num()) && (!(&force).is_truthy())) {
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(false)); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("blocked_by"), deps.clone()); __m.insert(Cow::Borrowed("requires_root"), SVal::Bool(false)); __m.insert(Cow::Borrowed("error"), (((SVal::Str(Cow::Borrowed("cannot remove ")) + target_id.clone()) + SVal::Str(Cow::Borrowed(": required by "))) + __st_join(&deps, &(SVal::Str(Cow::Borrowed(", ")))))); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    let mut scope = SVal::Str(Cow::Borrowed("system"));
    let mut path = SVal::Str(Cow::Borrowed(""));
    if (&(__st_has_key(&catalogue, &target_id))).is_truthy() {
        let mut entry = (&catalogue).get_ref(&target_id);
        if ((&(__st_has_key(&entry, &(SVal::Str(Cow::Borrowed("scope")))))).is_truthy() && (&((&entry).get_key("scope")) != &(SVal::Null))) {
            scope = (&entry).get_key("scope");
        }
        if ((&(__st_has_key(&entry, &(SVal::Str(Cow::Borrowed("install_path")))))).is_truthy() && (&((&entry).get_key("install_path")) != &(SVal::Null))) {
            path = (&entry).get_key("install_path");
        }
    }
    return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(true)); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![{ let mut __m = SMap::new(); __m.insert(Cow::Borrowed("id"), target_id.clone()); __m.insert(Cow::Borrowed("action"), SVal::Str(Cow::Borrowed("remove"))); __m.insert(Cow::Borrowed("from"), __st_str(&((&installed).get_ref(&target_id)))); __m.insert(Cow::Borrowed("to"), SVal::Str(Cow::Borrowed(""))); __m.insert(Cow::Borrowed("scope"), scope.clone()); __m.insert(Cow::Borrowed("install_path"), path.clone()); __m.insert(Cow::Borrowed("url"), SVal::Str(Cow::Borrowed(""))); __m.insert(Cow::Borrowed("sha256"), SVal::Str(Cow::Borrowed(""))); SVal::Map(Rc::new(RefCell::new(__m))) }])))); __m.insert(Cow::Borrowed("blocked_by"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("requires_root"), requires_privilege_escalation(scope.clone())); __m.insert(Cow::Borrowed("error"), SVal::Str(Cow::Borrowed(""))); SVal::Map(Rc::new(RefCell::new(__m))) };
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_plan_remove(__a: Vec<SVal>) -> SVal { plan_remove(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null), __a.get(2).cloned().unwrap_or(SVal::Null), __a.get(3).cloned().unwrap_or(SVal::Null)) }

pub fn plan_system_snapshot_transition(mut installed: SVal, mut catalogue: SVal, mut target_snapshot: SVal) -> SVal {
    let keys = SVal::Func(Rc::new(|args: Vec<SVal>| { if let Some(SVal::Map(m)) = args.get(0) { let ks = m.borrow().keys().map(|k| SVal::Str(k.clone())).collect(); return SVal::Arr(Rc::new(RefCell::new(ks))); } SVal::Null }));
    let mut target_ids = __st_sort(&((&keys).call(vec![target_snapshot.clone()])));
    let mut actions = SVal::Arr(Rc::new(RefCell::new(vec![])));
    let mut skipped = SVal::Arr(Rc::new(RefCell::new(vec![])));
    let mut needs_root = SVal::Bool(false);
    let mut inst_ids = __st_sort(&((&keys).call(vec![installed.clone()])));
    let mut i = SVal::Num(0.000000);
    while ((&i).as_num() < (&(__st_len(&inst_ids))).as_num()) {
        let mut id = (&inst_ids).get_ref(&i);
        if (!(&(__st_has_key(&target_snapshot, &id))).is_truthy()) {
            let mut scope = SVal::Str(Cow::Borrowed("system"));
            let mut path = SVal::Str(Cow::Borrowed(""));
            if (&(__st_has_key(&catalogue, &id))).is_truthy() {
                let mut entry = (&catalogue).get_ref(&id);
                if ((&(__st_has_key(&entry, &(SVal::Str(Cow::Borrowed("scope")))))).is_truthy() && (&((&entry).get_key("scope")) != &(SVal::Null))) {
                    scope = (&entry).get_key("scope");
                }
                if ((&(__st_has_key(&entry, &(SVal::Str(Cow::Borrowed("install_path")))))).is_truthy() && (&((&entry).get_key("install_path")) != &(SVal::Null))) {
                    path = (&entry).get_key("install_path");
                }
            }
            if (&(requires_privilege_escalation(scope.clone()))).is_truthy() {
                needs_root = SVal::Bool(true);
            }
            __st_push(&actions, { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("id"), id.clone()); __m.insert(Cow::Borrowed("action"), SVal::Str(Cow::Borrowed("remove"))); __m.insert(Cow::Borrowed("from"), __st_str(&((&installed).get_ref(&id)))); __m.insert(Cow::Borrowed("to"), SVal::Str(Cow::Borrowed(""))); __m.insert(Cow::Borrowed("scope"), scope.clone()); __m.insert(Cow::Borrowed("install_path"), path.clone()); __m.insert(Cow::Borrowed("url"), SVal::Str(Cow::Borrowed(""))); __m.insert(Cow::Borrowed("sha256"), SVal::Str(Cow::Borrowed(""))); SVal::Map(Rc::new(RefCell::new(__m))) });
        }
        i = (i.clone() + SVal::Num(1.000000));
    }
    let mut j = SVal::Num(0.000000);
    while ((&j).as_num() < (&(__st_len(&target_ids))).as_num()) {
        let mut id = (&target_ids).get_ref(&j);
        let mut want_ver = __st_str(&((&target_snapshot).get_ref(&id)));
        let mut cur_ver = (if (&(__st_has_key(&installed, &id))).is_truthy() { __st_str(&((&installed).get_ref(&id))) } else { SVal::Str(Cow::Borrowed("")) });
        let mut kind = classify_transition(cur_ver.clone(), want_ver.clone());
        if (&kind).eq_str("noop") {
            __st_push(&skipped, id.clone());
        } else {
            let mut scope = SVal::Str(Cow::Borrowed("system"));
            let mut path = SVal::Str(Cow::Borrowed(""));
            let mut url = SVal::Str(Cow::Borrowed(""));
            let mut sha = SVal::Str(Cow::Borrowed(""));
            if (&(__st_has_key(&catalogue, &id))).is_truthy() {
                let mut entry = (&catalogue).get_ref(&id);
                if ((&(__st_has_key(&entry, &(SVal::Str(Cow::Borrowed("scope")))))).is_truthy() && (&((&entry).get_key("scope")) != &(SVal::Null))) {
                    scope = (&entry).get_key("scope");
                }
                if ((&(__st_has_key(&entry, &(SVal::Str(Cow::Borrowed("install_path")))))).is_truthy() && (&((&entry).get_key("install_path")) != &(SVal::Null))) {
                    path = (&entry).get_key("install_path");
                }
                if ((&(__st_has_key(&entry, &(SVal::Str(Cow::Borrowed("url")))))).is_truthy() && (&((&entry).get_key("url")) != &(SVal::Null))) {
                    url = (&entry).get_key("url");
                }
                if ((&(__st_has_key(&entry, &(SVal::Str(Cow::Borrowed("sha256")))))).is_truthy() && (&((&entry).get_key("sha256")) != &(SVal::Null))) {
                    sha = (&entry).get_key("sha256");
                }
            }
            let mut pchk = validate_install_path(path.clone(), scope.clone());
            if (!(&((&pchk).get_key("ok"))).is_truthy()) {
                return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(false)); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("skipped"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("requires_root"), SVal::Bool(false)); __m.insert(Cow::Borrowed("error"), ((id.clone() + SVal::Str(Cow::Borrowed(": "))) + (&pchk).get_key("reason"))); SVal::Map(Rc::new(RefCell::new(__m))) };
            }
            if (&(requires_privilege_escalation(scope.clone()))).is_truthy() {
                needs_root = SVal::Bool(true);
            }
            __st_push(&actions, { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("id"), id.clone()); __m.insert(Cow::Borrowed("action"), kind.clone()); __m.insert(Cow::Borrowed("from"), cur_ver.clone()); __m.insert(Cow::Borrowed("to"), want_ver.clone()); __m.insert(Cow::Borrowed("scope"), scope.clone()); __m.insert(Cow::Borrowed("install_path"), path.clone()); __m.insert(Cow::Borrowed("url"), url.clone()); __m.insert(Cow::Borrowed("sha256"), sha.clone()); SVal::Map(Rc::new(RefCell::new(__m))) });
        }
        j = (j.clone() + SVal::Num(1.000000));
    }
    return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(true)); __m.insert(Cow::Borrowed("actions"), actions.clone()); __m.insert(Cow::Borrowed("skipped"), skipped.clone()); __m.insert(Cow::Borrowed("requires_root"), needs_root.clone()); __m.insert(Cow::Borrowed("error"), SVal::Str(Cow::Borrowed(""))); SVal::Map(Rc::new(RefCell::new(__m))) };
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_plan_system_snapshot_transition(__a: Vec<SVal>) -> SVal { plan_system_snapshot_transition(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null), __a.get(2).cloned().unwrap_or(SVal::Null)) }

pub fn db_default_state() -> SVal {
    return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("schema_version"), SVal::Num(2.000000)); __m.insert(Cow::Borrowed("installed"), SVal::Map(Rc::new(RefCell::new(SMap::new())))); __m.insert(Cow::Borrowed("ownership"), SVal::Map(Rc::new(RefCell::new(SMap::new())))); __m.insert(Cow::Borrowed("journal"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("config"), { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("channel"), SVal::Str(Cow::Borrowed("stable"))); __m.insert(Cow::Borrowed("auto_check"), SVal::Bool(true)); __m.insert(Cow::Borrowed("catalogue_url"), SVal::Str(Cow::Borrowed("https://updates.infinityos.uk/catalogue.json"))); __m.insert(Cow::Borrowed("cache_ttl_sec"), SVal::Num(3600.000000)); SVal::Map(Rc::new(RefCell::new(__m))) }); SVal::Map(Rc::new(RefCell::new(__m))) };
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_db_default_state(__a: Vec<SVal>) -> SVal { db_default_state() }

pub fn db_load_from_path(mut db_path: SVal) -> SVal {
    let type_fn = SVal::Func(Rc::new(|args: Vec<SVal>| SVal::Str(Cow::Borrowed(match args.first() { Some(SVal::Null) | None => "null", Some(SVal::Bool(_)) => "boolean", Some(SVal::Num(_)) => "number", Some(SVal::Str(_)) => "string", Some(SVal::Arr(_)) => "array", Some(SVal::Map(_)) => "map", Some(SVal::Func(_)) => "function" }))));
    let read_file = SVal::Func(Rc::new(|args: Vec<SVal>| { let p = args.get(0).map(|a| a.to_string()).unwrap_or_default(); SVal::Str(Cow::Owned(std::fs::read_to_string(&p).unwrap_or_default())) }));
    let file_exists = SVal::Func(Rc::new(|args: Vec<SVal>| { let p = args.get(0).map(|a| a.to_string()).unwrap_or_default(); SVal::Bool(std::path::Path::new(&p).exists()) }));
    if (!(&((&file_exists).call(vec![db_path.clone()]))).is_truthy()) {
        return db_default_state();
    }
    let mut raw = (&read_file).call(vec![db_path.clone()]);
    if (&(__st_len(&(__st_trim(&raw))))).eq_num(0.000000) {
        return db_default_state();
    }
    let mut parsed = __st_json_parse(&raw);
    if ((&parsed == &(SVal::Null)) || (!(&((&(type_fn.clone())).call(vec![parsed.clone()]))).eq_str("map"))) {
        return db_default_state();
    }
    if (!(&(__st_has_key(&parsed, &(SVal::Str(Cow::Borrowed("installed")))))).is_truthy()) {
        (&parsed).set_key("installed", SVal::Map(Rc::new(RefCell::new(SMap::new()))));
    }
    if (!(&(__st_has_key(&parsed, &(SVal::Str(Cow::Borrowed("ownership")))))).is_truthy()) {
        (&parsed).set_key("ownership", SVal::Map(Rc::new(RefCell::new(SMap::new()))));
    }
    if (!(&(__st_has_key(&parsed, &(SVal::Str(Cow::Borrowed("journal")))))).is_truthy()) {
        (&parsed).set_key("journal", SVal::Arr(Rc::new(RefCell::new(vec![]))));
    }
    if (!(&(__st_has_key(&parsed, &(SVal::Str(Cow::Borrowed("config")))))).is_truthy()) {
        (&parsed).set_key("config", (&(db_default_state())).get_key("config"));
    }
    return parsed.clone();
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_db_load_from_path(__a: Vec<SVal>) -> SVal { db_load_from_path(__a.get(0).cloned().unwrap_or(SVal::Null)) }

pub fn db_save_to_path(mut db_path: SVal, mut state: SVal) -> SVal {
    let write_file = SVal::Func(Rc::new(|args: Vec<SVal>| { let p = args.get(0).map(|a| a.to_string()).unwrap_or_default(); let c = args.get(1).map(|a| a.to_string()).unwrap_or_default(); SVal::Bool(std::fs::write(&p, c).is_ok()) }));
    return (&write_file).call(vec![db_path.clone(), __st_json_stringify(&state)]);
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_db_save_to_path(__a: Vec<SVal>) -> SVal { db_save_to_path(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null)) }

pub fn db_apply_transaction(mut state: SVal, mut actions: SVal, mut timestamp: SVal) -> SVal {
    let mut tx_id = (SVal::Str(Cow::Borrowed("tx-")) + __st_str(&((__st_len(&((&state).get_key("journal"))) + SVal::Num(1.000000)))));
    let mut installed = (&state).get_key("installed");
    let mut record_ops = SVal::Arr(Rc::new(RefCell::new(vec![])));
    let mut i = SVal::Num(0.000000);
    while ((&i).as_num() < (&(__st_len(&actions))).as_num()) {
        let mut a = (&actions).get_ref(&i);
        let mut id = (&a).get_key("id");
        let mut kind = (&a).get_key("action");
        let mut prev_ver = (if (&(__st_has_key(&installed, &id))).is_truthy() { __st_str(&((&installed).get_ref(&id))) } else { SVal::Str(Cow::Borrowed("")) });
        if (&kind).eq_str("remove") {
            __st_delete_key(&installed, &id);
        } else {
            (&installed).set(id.clone(), (&a).get_key("to"));
        }
        __st_push(&record_ops, { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("id"), id.clone()); __m.insert(Cow::Borrowed("action"), kind.clone()); __m.insert(Cow::Borrowed("from"), prev_ver.clone()); __m.insert(Cow::Borrowed("to"), (&a).get_key("to")); __m.insert(Cow::Borrowed("install_path"), (&a).get_key("install_path")); SVal::Map(Rc::new(RefCell::new(__m))) });
        i = (i.clone() + SVal::Num(1.000000));
    }
    let mut entry = { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("tx_id"), tx_id.clone()); __m.insert(Cow::Borrowed("timestamp"), timestamp.clone()); __m.insert(Cow::Borrowed("status"), SVal::Str(Cow::Borrowed("committed"))); __m.insert(Cow::Borrowed("ops"), record_ops.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
    __st_push(&((&state).get_key("journal")), entry.clone());
    return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(true)); __m.insert(Cow::Borrowed("tx_id"), tx_id.clone()); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_db_apply_transaction(__a: Vec<SVal>) -> SVal { db_apply_transaction(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null), __a.get(2).cloned().unwrap_or(SVal::Null)) }

pub fn db_rollback_last(mut state: SVal, mut timestamp: SVal) -> SVal {
    let mut journal = (&state).get_key("journal");
    if (&(__st_len(&journal))).eq_num(0.000000) {
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(false)); __m.insert(Cow::Borrowed("error"), SVal::Str(Cow::Borrowed("no transactions in journal"))); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    let mut idx = (__st_len(&journal) - SVal::Num(1.000000));
    while (((&idx).as_num() >= (&(SVal::Num(0.000000))).as_num()) && (!(&((&((&journal).get_ref(&idx))).get_key("status"))).eq_str("committed"))) {
        idx = (idx.clone() - SVal::Num(1.000000));
    }
    if ((&idx).as_num() < (&(SVal::Num(0.000000))).as_num()) {
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(false)); __m.insert(Cow::Borrowed("error"), SVal::Str(Cow::Borrowed("no committed transaction to roll back"))); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    let mut tx = (&journal).get_ref(&idx);
    let mut ops = (&tx).get_key("ops");
    let mut installed = (&state).get_key("installed");
    let mut reverted = SVal::Arr(Rc::new(RefCell::new(vec![])));
    let mut j = (__st_len(&ops) - SVal::Num(1.000000));
    while ((&j).as_num() >= (&(SVal::Num(0.000000))).as_num()) {
        let mut op = (&ops).get_ref(&j);
        let mut id = (&op).get_key("id");
        let mut from_ver = (&op).get_key("from");
        if (&(__st_len(&from_ver))).eq_num(0.000000) {
            __st_delete_key(&installed, &id);
        } else {
            (&installed).set(id.clone(), from_ver.clone());
        }
        __st_push(&reverted, SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("id"), id.clone()), (Cow::Borrowed("restored_version"), from_ver.clone())])))));
        j = (j.clone() - SVal::Num(1.000000));
    }
    (&tx).set_key("status", SVal::Str(Cow::Borrowed("rolled_back")));
    (&tx).set_key("rolled_back_at", timestamp.clone());
    return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(true)); __m.insert(Cow::Borrowed("tx_id"), (&tx).get_key("tx_id")); __m.insert(Cow::Borrowed("reverted"), reverted.clone()); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_db_rollback_last(__a: Vec<SVal>) -> SVal { db_rollback_last(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null)) }

pub fn db_set_config(mut state: SVal, mut key: SVal, mut value: SVal) -> SVal {
    let mut cfg = (&state).get_key("config");
    if (&key).eq_str("channel") {
        if (((!(&value).eq_str("stable")) && (!(&value).eq_str("beta"))) && (!(&value).eq_str("nightly"))) {
            return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(false)); __m.insert(Cow::Borrowed("error"), (SVal::Str(Cow::Borrowed("invalid channel: ")) + __st_str(&value))); SVal::Map(Rc::new(RefCell::new(__m))) };
        }
        (&cfg).set_key("channel", value.clone());
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(true)); __m.insert(Cow::Borrowed("error"), SVal::Str(Cow::Borrowed(""))); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    if (&key).eq_str("auto_check") {
        (&cfg).set_key("auto_check", SVal::Bool(((&value).eq_str("true") || (&value == &(SVal::Bool(true))))));
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(true)); __m.insert(Cow::Borrowed("error"), SVal::Str(Cow::Borrowed(""))); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    if (&key).eq_str("catalogue_url") {
        if (!(&(__st_starts_with(&(__st_str(&value)), &(SVal::Str(Cow::Borrowed("https://")))))).is_truthy()) {
            return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(false)); __m.insert(Cow::Borrowed("error"), SVal::Str(Cow::Borrowed("catalogue_url must use https://"))); SVal::Map(Rc::new(RefCell::new(__m))) };
        }
        (&cfg).set_key("catalogue_url", __st_str(&value));
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(true)); __m.insert(Cow::Borrowed("error"), SVal::Str(Cow::Borrowed(""))); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    if (&key).eq_str("cache_ttl_sec") {
        let mut n = SVal::Num((&(SVal::Num((&value).as_num()))).as_num().trunc());
        if ((&n).as_num() < (&(SVal::Num(60.000000))).as_num()) {
            return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(false)); __m.insert(Cow::Borrowed("error"), SVal::Str(Cow::Borrowed("cache_ttl_sec must be at least 60"))); SVal::Map(Rc::new(RefCell::new(__m))) };
        }
        (&cfg).set_key("cache_ttl_sec", n.clone());
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(true)); __m.insert(Cow::Borrowed("error"), SVal::Str(Cow::Borrowed(""))); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(false)); __m.insert(Cow::Borrowed("error"), (SVal::Str(Cow::Borrowed("unknown config key: ")) + __st_str(&key))); SVal::Map(Rc::new(RefCell::new(__m))) };
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_db_set_config(__a: Vec<SVal>) -> SVal { db_set_config(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null), __a.get(2).cloned().unwrap_or(SVal::Null)) }

pub fn parse_cli_flags(mut args: SVal) -> SVal {
    let mut positional = SVal::Arr(Rc::new(RefCell::new(vec![])));
    let mut flags = { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("json"), SVal::Bool(false)); __m.insert(Cow::Borrowed("dry_run"), SVal::Bool(false)); __m.insert(Cow::Borrowed("force"), SVal::Bool(false)); __m.insert(Cow::Borrowed("channel"), SVal::Str(Cow::Borrowed(""))); SVal::Map(Rc::new(RefCell::new(__m))) };
    let mut i = SVal::Num(0.000000);
    while ((&i).as_num() < (&(__st_len(&args))).as_num()) {
        let mut a = (&args).get_ref(&i);
        if (&a).eq_str("--json") {
            (&flags).set_key("json", SVal::Bool(true));
        } else {
            if (&a).eq_str("--dry-run") {
                (&flags).set_key("dry_run", SVal::Bool(true));
            } else {
                if ((&a).eq_str("--force") || (&a).eq_str("-f")) {
                    (&flags).set_key("force", SVal::Bool(true));
                } else {
                    if ((&a).eq_str("--channel") && ((&((i.clone() + SVal::Num(1.000000)))).as_num() < (&(__st_len(&args))).as_num())) {
                        i = (i.clone() + SVal::Num(1.000000));
                        (&flags).set_key("channel", (&args).get_ref(&i));
                    } else {
                        __st_push(&positional, a.clone());
                    }
                }
            }
        }
        i = (i.clone() + SVal::Num(1.000000));
    }
    return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("positional"), positional.clone()); __m.insert(Cow::Borrowed("flags"), flags.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_parse_cli_flags(__a: Vec<SVal>) -> SVal { parse_cli_flags(__a.get(0).cloned().unwrap_or(SVal::Null)) }

pub fn format_help_text() -> SVal {
    let mut lines = SVal::Arr(Rc::new(RefCell::new(vec![SVal::Str(Cow::Borrowed("InfinityOS Updater (Steel-compiled core)")), SVal::Str(Cow::Borrowed("")), SVal::Str(Cow::Borrowed("Usage: infinity-updater <command> [args] [flags]")), SVal::Str(Cow::Borrowed("")), SVal::Str(Cow::Borrowed("Commands:")), SVal::Str(Cow::Borrowed("  status              Show installed components and pending updates")), SVal::Str(Cow::Borrowed("  list                List installed components")), SVal::Str(Cow::Borrowed("  search [query]      Search catalogue components")), SVal::Str(Cow::Borrowed("  check               Check for available updates")), SVal::Str(Cow::Borrowed("  install <id...>     Plan/install one or more components")), SVal::Str(Cow::Borrowed("  upgrade [id...]     Plan/upgrade installed components")), SVal::Str(Cow::Borrowed("  remove <id>         Plan/remove an installed component")), SVal::Str(Cow::Borrowed("  rollback            Roll back the most recent transaction")), SVal::Str(Cow::Borrowed("  history             Show transaction journal")), SVal::Str(Cow::Borrowed("  verify              Validate catalogue schema and security policy")), SVal::Str(Cow::Borrowed("  config [k] [v]      Show or update configuration")), SVal::Str(Cow::Borrowed("")), SVal::Str(Cow::Borrowed("Flags:")), SVal::Str(Cow::Borrowed("  --json              Emit machine-readable JSON output")), SVal::Str(Cow::Borrowed("  --dry-run           Plan actions without modifying state")), SVal::Str(Cow::Borrowed("  --force, -f         Bypass reverse-dependency checks on remove")), SVal::Str(Cow::Borrowed("  --channel <name>    Filter channel (stable|beta|nightly)"))])));
    return __st_join(&lines, &(SVal::Str(Cow::Borrowed("\n"))));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_format_help_text(__a: Vec<SVal>) -> SVal { format_help_text() }

pub fn format_plan_lines(mut plan: SVal) -> SVal {
    if (!(&((&plan).get_key("ok"))).is_truthy()) {
        return (SVal::Str(Cow::Borrowed("Error: ")) + __st_str(&((&plan).get_key("error"))));
    }
    let mut lines = SVal::Arr(Rc::new(RefCell::new(vec![])));
    let mut acts = (&plan).get_key("actions");
    let mut i = SVal::Num(0.000000);
    while ((&i).as_num() < (&(__st_len(&acts))).as_num()) {
        let mut a = (&acts).get_ref(&i);
        let mut arrow = (if ((&(__st_len(&((&a).get_key("from"))))).as_num() > (&(SVal::Num(0.000000))).as_num()) { (((&a).get_key("from") + SVal::Str(Cow::Borrowed(" -> "))) + (&a).get_key("to")) } else { (&a).get_key("to") });
        if (&((&a).get_key("action"))).eq_str("remove") {
            arrow = (&a).get_key("from");
        }
        __st_push(&lines, (((((((SVal::Str(Cow::Borrowed("  [")) + __st_upper(&((&a).get_key("action")))) + SVal::Str(Cow::Borrowed("] "))) + (&a).get_key("id")) + SVal::Str(Cow::Borrowed(" ("))) + arrow.clone()) + SVal::Str(Cow::Borrowed(") -> "))) + (&a).get_key("install_path")));
        i = (i.clone() + SVal::Num(1.000000));
    }
    if ((&(__st_len(&((&plan).get_key("skipped"))))).as_num() > (&(SVal::Num(0.000000))).as_num()) {
        __st_push(&lines, (SVal::Str(Cow::Borrowed("  [SKIP] already up to date: ")) + __st_join(&((&plan).get_key("skipped")), &(SVal::Str(Cow::Borrowed(", "))))));
    }
    if (&(__st_len(&lines))).eq_num(0.000000) {
        return SVal::Str(Cow::Borrowed("Nothing to do."));
    }
    return __st_join(&lines, &(SVal::Str(Cow::Borrowed("\n"))));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_format_plan_lines(__a: Vec<SVal>) -> SVal { format_plan_lines(__a.get(0).cloned().unwrap_or(SVal::Null)) }

pub fn cli_dispatch(mut args: SVal, mut state: SVal, mut catalogue: SVal, mut timestamp: SVal) -> SVal {
    let keys = SVal::Func(Rc::new(|args: Vec<SVal>| { if let Some(SVal::Map(m)) = args.get(0) { let ks = m.borrow().keys().map(|k| SVal::Str(k.clone())).collect(); return SVal::Arr(Rc::new(RefCell::new(ks))); } SVal::Null }));
    let pad_end = SVal::Func(Rc::new(|args: Vec<SVal>| { let s = args.get(0).map(|a| a.to_string()).unwrap_or_default(); let w = args.get(1).map(|a| (a.as_num() as isize).max(0) as usize).unwrap_or(0); let p = args.get(2).map(|a| a.to_string()).unwrap_or_else(|| " ".to_string()); let cur = s.chars().count(); if cur >= w || p.is_empty() { return SVal::Str(Cow::Owned(s)); } let pc: Vec<char> = p.chars().collect(); let mut out = s; for i in 0..(w - cur) { out.push(pc[i % pc.len()]); } SVal::Str(Cow::Owned(out)) }));
    let mut parsed = parse_cli_flags(args.clone());
    let mut pos = (&parsed).get_key("positional");
    let mut flags = (&parsed).get_key("flags");
    let mut channel = (if ((&(__st_len(&((&flags).get_key("channel"))))).as_num() > (&(SVal::Num(0.000000))).as_num()) { (&flags).get_key("channel") } else { (&((&state).get_key("config"))).get_key("channel") });
    if ((((&(__st_len(&pos))).eq_num(0.000000) || (&((&pos).get_ref(&(SVal::Num(0.000000))))).eq_str("help")) || (&((&pos).get_ref(&(SVal::Num(0.000000))))).eq_str("--help")) || (&((&pos).get_ref(&(SVal::Num(0.000000))))).eq_str("-h")) {
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(0.000000)); __m.insert(Cow::Borrowed("output"), format_help_text()); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("save_state"), SVal::Bool(false)); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    let mut cmd = (&pos).get_ref(&(SVal::Num(0.000000)));
    if (&cmd).eq_str("list") {
        let mut ks = __st_sort(&((&keys).call(vec![(&state).get_key("installed")])));
        if (&((&flags).get_key("json"))).is_truthy() {
            return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(0.000000)); __m.insert(Cow::Borrowed("output"), __st_json_stringify(&((&state).get_key("installed")))); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("save_state"), SVal::Bool(false)); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
        }
        if (&(__st_len(&ks))).eq_num(0.000000) {
            return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(0.000000)); __m.insert(Cow::Borrowed("output"), SVal::Str(Cow::Borrowed("No components installed."))); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("save_state"), SVal::Bool(false)); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
        }
        let mut lines = SVal::Arr(Rc::new(RefCell::new(vec![])));
        let mut i = SVal::Num(0.000000);
        while ((&i).as_num() < (&(__st_len(&ks))).as_num()) {
            let mut id = (&ks).get_ref(&i);
            __st_push(&lines, (((&pad_end).call(vec![id.clone(), SVal::Num(24.000000), SVal::Str(Cow::Borrowed(" "))]) + SVal::Str(Cow::Borrowed(" "))) + __st_str(&((&((&state).get_key("installed"))).get_ref(&id)))));
            i = (i.clone() + SVal::Num(1.000000));
        }
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(0.000000)); __m.insert(Cow::Borrowed("output"), __st_join(&lines, &(SVal::Str(Cow::Borrowed("\n"))))); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("save_state"), SVal::Bool(false)); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    if (&cmd).eq_str("search") {
        let mut q = (if ((&(__st_len(&pos))).as_num() > (&(SVal::Num(1.000000))).as_num()) { (&pos).get_ref(&(SVal::Num(1.000000))) } else { SVal::Str(Cow::Borrowed("")) });
        let mut hits = search_catalogue(catalogue.clone(), q.clone(), channel.clone());
        if (&((&flags).get_key("json"))).is_truthy() {
            return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(0.000000)); __m.insert(Cow::Borrowed("output"), __st_json_stringify(&hits)); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("save_state"), SVal::Bool(false)); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
        }
        if (&(__st_len(&hits))).eq_num(0.000000) {
            return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(0.000000)); __m.insert(Cow::Borrowed("output"), SVal::Str(Cow::Borrowed("No matching components."))); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("save_state"), SVal::Bool(false)); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
        }
        let mut lines = SVal::Arr(Rc::new(RefCell::new(vec![])));
        let mut i = SVal::Num(0.000000);
        while ((&i).as_num() < (&(__st_len(&hits))).as_num()) {
            let mut h = (&hits).get_ref(&i);
            __st_push(&lines, (((((&pad_end).call(vec![(&h).get_key("id"), SVal::Num(22.000000), SVal::Str(Cow::Borrowed(" "))]) + SVal::Str(Cow::Borrowed(" "))) + (&pad_end).call(vec![(&h).get_key("version"), SVal::Num(10.000000), SVal::Str(Cow::Borrowed(" "))])) + SVal::Str(Cow::Borrowed(" "))) + (&h).get_key("description")));
            i = (i.clone() + SVal::Num(1.000000));
        }
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(0.000000)); __m.insert(Cow::Borrowed("output"), __st_join(&lines, &(SVal::Str(Cow::Borrowed("\n"))))); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("save_state"), SVal::Bool(false)); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    if ((&cmd).eq_str("check") || (&cmd).eq_str("status")) {
        let mut chk = plan_check_updates((&state).get_key("installed"), catalogue.clone(), channel.clone());
        if (&((&flags).get_key("json"))).is_truthy() {
            return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(0.000000)); __m.insert(Cow::Borrowed("output"), __st_json_stringify(&chk)); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("save_state"), SVal::Bool(false)); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
        }
        let mut ups = (&chk).get_key("updates");
        if (&(__st_len(&ups))).eq_num(0.000000) {
            return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(0.000000)); __m.insert(Cow::Borrowed("output"), ((SVal::Str(Cow::Borrowed("All ")) + __st_str(&(__st_len(&((&chk).get_key("up_to_date")))))) + SVal::Str(Cow::Borrowed(" installed components are up to date.")))); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("save_state"), SVal::Bool(false)); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
        }
        let mut lines = SVal::Arr(Rc::new(RefCell::new(vec![((SVal::Str(Cow::Borrowed("Available updates (")) + __st_str(&(__st_len(&ups)))) + SVal::Str(Cow::Borrowed("):")))])));
        let mut i = SVal::Num(0.000000);
        while ((&i).as_num() < (&(__st_len(&ups))).as_num()) {
            let mut u = (&ups).get_ref(&i);
            __st_push(&lines, (((((SVal::Str(Cow::Borrowed("  ")) + (&u).get_key("id")) + SVal::Str(Cow::Borrowed(": "))) + (&u).get_key("from")) + SVal::Str(Cow::Borrowed(" -> "))) + (&u).get_key("to")));
            i = (i.clone() + SVal::Num(1.000000));
        }
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(0.000000)); __m.insert(Cow::Borrowed("output"), __st_join(&lines, &(SVal::Str(Cow::Borrowed("\n"))))); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("save_state"), SVal::Bool(false)); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    if (&cmd).eq_str("verify") {
        let mut v = validate_catalogue(catalogue.clone());
        let mut out = (if (&((&flags).get_key("json"))).is_truthy() { __st_json_stringify(&v) } else { (if (&((&v).get_key("ok"))).is_truthy() { ((SVal::Str(Cow::Borrowed("Catalogue OK (")) + __st_str(&((&v).get_key("count")))) + SVal::Str(Cow::Borrowed(" components)."))) } else { (SVal::Str(Cow::Borrowed("Catalogue errors:\n  ")) + __st_join(&((&v).get_key("errors")), &(SVal::Str(Cow::Borrowed("\n  "))))) }) });
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), (if (&((&v).get_key("ok"))).is_truthy() { SVal::Num(0.000000) } else { SVal::Num(1.000000) })); __m.insert(Cow::Borrowed("output"), out.clone()); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("save_state"), SVal::Bool(false)); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    if ((&cmd).eq_str("install") || (&cmd).eq_str("upgrade")) {
        let mut targets = __st_slice(&pos, &(SVal::Num(1.000000)), Some(&(__st_len(&pos))));
        if ((&cmd).eq_str("upgrade") && (&(__st_len(&targets))).eq_num(0.000000)) {
            targets = __st_sort(&((&keys).call(vec![(&state).get_key("installed")])));
        }
        if (&(__st_len(&targets))).eq_num(0.000000) {
            return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(2.000000)); __m.insert(Cow::Borrowed("output"), SVal::Str(Cow::Borrowed("Error: specify at least one component id"))); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("save_state"), SVal::Bool(false)); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
        }
        let mut plan = plan_reconcile((&state).get_key("installed"), catalogue.clone(), targets.clone());
        if (!(&((&plan).get_key("ok"))).is_truthy()) {
            let mut out = (if (&((&flags).get_key("json"))).is_truthy() { __st_json_stringify(&plan) } else { (SVal::Str(Cow::Borrowed("Error: ")) + (&plan).get_key("error")) });
            return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(1.000000)); __m.insert(Cow::Borrowed("output"), out.clone()); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("save_state"), SVal::Bool(false)); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
        }
        let mut should_save = SVal::Bool(((!(&((&flags).get_key("dry_run"))).is_truthy()) && ((&(__st_len(&((&plan).get_key("actions"))))).as_num() > (&(SVal::Num(0.000000))).as_num())));
        if (&should_save).is_truthy() {
            db_apply_transaction(state.clone(), (&plan).get_key("actions"), timestamp.clone());
        }
        let mut out = (if (&((&flags).get_key("json"))).is_truthy() { __st_json_stringify(&plan) } else { format_plan_lines(plan.clone()) });
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(0.000000)); __m.insert(Cow::Borrowed("output"), out.clone()); __m.insert(Cow::Borrowed("actions"), (if (&((&flags).get_key("dry_run"))).is_truthy() { SVal::Arr(Rc::new(RefCell::new(vec![]))) } else { (&plan).get_key("actions") })); __m.insert(Cow::Borrowed("save_state"), should_save.clone()); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    if (&cmd).eq_str("remove") {
        if ((&(__st_len(&pos))).as_num() < (&(SVal::Num(2.000000))).as_num()) {
            return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(2.000000)); __m.insert(Cow::Borrowed("output"), SVal::Str(Cow::Borrowed("Error: specify a component id to remove"))); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("save_state"), SVal::Bool(false)); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
        }
        let mut plan = plan_remove((&state).get_key("installed"), catalogue.clone(), (&pos).get_ref(&(SVal::Num(1.000000))), (&flags).get_key("force"));
        if (!(&((&plan).get_key("ok"))).is_truthy()) {
            let mut out = (if (&((&flags).get_key("json"))).is_truthy() { __st_json_stringify(&plan) } else { (SVal::Str(Cow::Borrowed("Error: ")) + (&plan).get_key("error")) });
            return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(1.000000)); __m.insert(Cow::Borrowed("output"), out.clone()); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("save_state"), SVal::Bool(false)); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
        }
        let mut should_save = SVal::Bool(!(&((&flags).get_key("dry_run"))).is_truthy());
        if (&should_save).is_truthy() {
            db_apply_transaction(state.clone(), (&plan).get_key("actions"), timestamp.clone());
        }
        let mut out = (if (&((&flags).get_key("json"))).is_truthy() { __st_json_stringify(&plan) } else { format_plan_lines(plan.clone()) });
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(0.000000)); __m.insert(Cow::Borrowed("output"), out.clone()); __m.insert(Cow::Borrowed("actions"), (if (&((&flags).get_key("dry_run"))).is_truthy() { SVal::Arr(Rc::new(RefCell::new(vec![]))) } else { (&plan).get_key("actions") })); __m.insert(Cow::Borrowed("save_state"), should_save.clone()); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    if (&cmd).eq_str("rollback") {
        let mut rb = db_rollback_last(state.clone(), timestamp.clone());
        if (!(&((&rb).get_key("ok"))).is_truthy()) {
            let mut out = (if (&((&flags).get_key("json"))).is_truthy() { __st_json_stringify(&rb) } else { (SVal::Str(Cow::Borrowed("Error: ")) + (&rb).get_key("error")) });
            return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(1.000000)); __m.insert(Cow::Borrowed("output"), out.clone()); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("save_state"), SVal::Bool(false)); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
        }
        let mut out = (if (&((&flags).get_key("json"))).is_truthy() { __st_json_stringify(&rb) } else { ((((SVal::Str(Cow::Borrowed("Rolled back ")) + (&rb).get_key("tx_id")) + SVal::Str(Cow::Borrowed(" ("))) + __st_str(&(__st_len(&((&rb).get_key("reverted")))))) + SVal::Str(Cow::Borrowed(" components restored)."))) });
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(0.000000)); __m.insert(Cow::Borrowed("output"), out.clone()); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("save_state"), SVal::Bool(!(&((&flags).get_key("dry_run"))).is_truthy())); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    if (&cmd).eq_str("history") {
        if (&((&flags).get_key("json"))).is_truthy() {
            return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(0.000000)); __m.insert(Cow::Borrowed("output"), __st_json_stringify(&((&state).get_key("journal")))); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("save_state"), SVal::Bool(false)); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
        }
        let mut j = (&state).get_key("journal");
        if (&(__st_len(&j))).eq_num(0.000000) {
            return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(0.000000)); __m.insert(Cow::Borrowed("output"), SVal::Str(Cow::Borrowed("No transactions recorded."))); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("save_state"), SVal::Bool(false)); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
        }
        let mut lines = SVal::Arr(Rc::new(RefCell::new(vec![])));
        let mut i = SVal::Num(0.000000);
        while ((&i).as_num() < (&(__st_len(&j))).as_num()) {
            let mut tx = (&j).get_ref(&i);
            __st_push(&lines, (((((((&tx).get_key("tx_id") + SVal::Str(Cow::Borrowed(" ["))) + (&tx).get_key("status")) + SVal::Str(Cow::Borrowed("] ops="))) + __st_str(&(__st_len(&((&tx).get_key("ops")))))) + SVal::Str(Cow::Borrowed(" ts="))) + __st_str(&((&tx).get_key("timestamp")))));
            i = (i.clone() + SVal::Num(1.000000));
        }
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(0.000000)); __m.insert(Cow::Borrowed("output"), __st_join(&lines, &(SVal::Str(Cow::Borrowed("\n"))))); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("save_state"), SVal::Bool(false)); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    if (&cmd).eq_str("config") {
        if (&(__st_len(&pos))).eq_num(1.000000) {
            return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(0.000000)); __m.insert(Cow::Borrowed("output"), __st_json_stringify(&((&state).get_key("config")))); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("save_state"), SVal::Bool(false)); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
        }
        if ((&(__st_len(&pos))).as_num() < (&(SVal::Num(3.000000))).as_num()) {
            return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(2.000000)); __m.insert(Cow::Borrowed("output"), SVal::Str(Cow::Borrowed("Usage: infinity-updater config <key> <value>"))); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("save_state"), SVal::Bool(false)); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
        }
        let mut res = db_set_config(state.clone(), (&pos).get_ref(&(SVal::Num(1.000000))), (&pos).get_ref(&(SVal::Num(2.000000))));
        if (!(&((&res).get_key("ok"))).is_truthy()) {
            return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(1.000000)); __m.insert(Cow::Borrowed("output"), (SVal::Str(Cow::Borrowed("Error: ")) + (&res).get_key("error"))); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("save_state"), SVal::Bool(false)); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
        }
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(0.000000)); __m.insert(Cow::Borrowed("output"), (((SVal::Str(Cow::Borrowed("Updated ")) + (&pos).get_ref(&(SVal::Num(1.000000)))) + SVal::Str(Cow::Borrowed(" = "))) + (&pos).get_ref(&(SVal::Num(2.000000))))); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("save_state"), SVal::Bool(true)); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(2.000000)); __m.insert(Cow::Borrowed("output"), ((SVal::Str(Cow::Borrowed("Unknown command: ")) + cmd.clone()) + SVal::Str(Cow::Borrowed("\nRun 'infinity-updater help' for usage.")))); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("save_state"), SVal::Bool(false)); __m.insert(Cow::Borrowed("state"), state.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_cli_dispatch(__a: Vec<SVal>) -> SVal { cli_dispatch(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null), __a.get(2).cloned().unwrap_or(SVal::Null), __a.get(3).cloned().unwrap_or(SVal::Null)) }

pub fn ui_initial_state() -> SVal {
    return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("active_tab"), SVal::Str(Cow::Borrowed("updates"))); __m.insert(Cow::Borrowed("search_query"), SVal::Str(Cow::Borrowed(""))); __m.insert(Cow::Borrowed("selected_ids"), SVal::Arr(Rc::new(RefCell::new(vec![])))); __m.insert(Cow::Borrowed("busy"), SVal::Bool(false)); __m.insert(Cow::Borrowed("banner_level"), SVal::Str(Cow::Borrowed("info"))); __m.insert(Cow::Borrowed("banner_text"), SVal::Str(Cow::Borrowed("Ready"))); __m.insert(Cow::Borrowed("pending_plan"), SVal::Null); SVal::Map(Rc::new(RefCell::new(__m))) };
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_ui_initial_state(__a: Vec<SVal>) -> SVal { ui_initial_state() }

pub fn ui_toggle_selection(mut selected: SVal, mut id: SVal) -> SVal {
    let mut out = SVal::Arr(Rc::new(RefCell::new(vec![])));
    let mut found = SVal::Bool(false);
    let mut i = SVal::Num(0.000000);
    while ((&i).as_num() < (&(__st_len(&selected))).as_num()) {
        if (&((&selected).get_ref(&i)) == &id) {
            found = SVal::Bool(true);
        } else {
            __st_push(&out, (&selected).get_ref(&i));
        }
        i = (i.clone() + SVal::Num(1.000000));
    }
    if (!(&found).is_truthy()) {
        __st_push(&out, id.clone());
    }
    return out.clone();
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_ui_toggle_selection(__a: Vec<SVal>) -> SVal { ui_toggle_selection(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null)) }

pub fn ui_reduce(mut ui_state: SVal, mut event: SVal, mut db_state: SVal, mut catalogue: SVal) -> SVal {
    let mut kind = (&event).get_key("type");
    if (&kind).eq_str("switch_tab") {
        let mut tab = (&event).get_key("tab");
        if (((((&tab).eq_str("updates") || (&tab).eq_str("installed")) || (&tab).eq_str("explore")) || (&tab).eq_str("history")) || (&tab).eq_str("settings")) {
            (&ui_state).set_key("active_tab", tab.clone());
        }
        return ui_state.clone();
    }
    if (&kind).eq_str("set_search") {
        (&ui_state).set_key("search_query", __st_str(&((&event).get_key("query"))));
        return ui_state.clone();
    }
    if (&kind).eq_str("toggle_select") {
        (&ui_state).set_key("selected_ids", ui_toggle_selection((&ui_state).get_key("selected_ids"), __st_str(&((&event).get_key("id")))));
        return ui_state.clone();
    }
    if (&kind).eq_str("select_all_updates") {
        let mut chk = plan_check_updates((&db_state).get_key("installed"), catalogue.clone(), (&((&db_state).get_key("config"))).get_key("channel"));
        let mut ids = SVal::Arr(Rc::new(RefCell::new(vec![])));
        let mut ups = (&chk).get_key("updates");
        let mut i = SVal::Num(0.000000);
        while ((&i).as_num() < (&(__st_len(&ups))).as_num()) {
            __st_push(&ids, (&((&ups).get_ref(&i))).get_key("id"));
            i = (i.clone() + SVal::Num(1.000000));
        }
        (&ui_state).set_key("selected_ids", ids.clone());
        (&ui_state).set_key("banner_level", SVal::Str(Cow::Borrowed("info")));
        (&ui_state).set_key("banner_text", (__st_str(&(__st_len(&ids))) + SVal::Str(Cow::Borrowed(" updates selected"))));
        return ui_state.clone();
    }
    if (&kind).eq_str("preview_plan") {
        let mut plan = plan_reconcile((&db_state).get_key("installed"), catalogue.clone(), (&ui_state).get_key("selected_ids"));
        (&ui_state).set_key("pending_plan", plan.clone());
        if (!(&((&plan).get_key("ok"))).is_truthy()) {
            (&ui_state).set_key("banner_level", SVal::Str(Cow::Borrowed("error")));
            (&ui_state).set_key("banner_text", (&plan).get_key("error"));
        } else {
            (&ui_state).set_key("banner_level", SVal::Str(Cow::Borrowed("success")));
            (&ui_state).set_key("banner_text", ((SVal::Str(Cow::Borrowed("Ready to apply ")) + __st_str(&(__st_len(&((&plan).get_key("actions")))))) + SVal::Str(Cow::Borrowed(" actions"))));
        }
        return ui_state.clone();
    }
    if (&kind).eq_str("clear_selection") {
        (&ui_state).set_key("selected_ids", SVal::Arr(Rc::new(RefCell::new(vec![]))));
        (&ui_state).set_key("pending_plan", SVal::Null);
        (&ui_state).set_key("banner_level", SVal::Str(Cow::Borrowed("info")));
        (&ui_state).set_key("banner_text", SVal::Str(Cow::Borrowed("Selection cleared")));
        return ui_state.clone();
    }
    return ui_state.clone();
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_ui_reduce(__a: Vec<SVal>) -> SVal { ui_reduce(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null), __a.get(2).cloned().unwrap_or(SVal::Null), __a.get(3).cloned().unwrap_or(SVal::Null)) }

pub fn build_package_manifest(mut id: SVal, mut version: SVal, mut scope: SVal, mut install_path: SVal, mut sha256: SVal, mut signature: SVal, mut files: SVal) -> SVal {
    let sign = SVal::Func(Rc::new(|args: Vec<SVal>| { let x = args.get(0).map(|a| a.as_num()).unwrap_or(0.0); SVal::Num(if x > 0.0 { 1.0 } else if x < 0.0 { -1.0 } else { 0.0 }) }));
    let mut entry_check = validate_catalogue_entry(id.clone(), { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("version"), version.clone()); __m.insert(Cow::Borrowed("scope"), scope.clone()); __m.insert(Cow::Borrowed("install_path"), install_path.clone()); __m.insert(Cow::Borrowed("sha256"), sha256.clone()); SVal::Map(Rc::new(RefCell::new(__m))) });
    if (!(&((&entry_check).get_key("ok"))).is_truthy()) {
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(false)); __m.insert(Cow::Borrowed("error"), (&entry_check).get_key("reason")); __m.insert(Cow::Borrowed("manifest"), SVal::Null); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    let mut arch_check = validate_archive_manifest(files.clone());
    if (!(&((&arch_check).get_key("ok"))).is_truthy()) {
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(false)); __m.insert(Cow::Borrowed("error"), (&arch_check).get_key("reason")); __m.insert(Cow::Borrowed("manifest"), SVal::Null); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    if (((&signature != &(SVal::Null)) && ((&(__st_len(&(__st_str(&signature))))).as_num() > (&(SVal::Num(0.000000))).as_num())) && (!(&(validate_ed25519_sig(__st_str(&signature)))).is_truthy())) {
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(false)); __m.insert(Cow::Borrowed("error"), SVal::Str(Cow::Borrowed("invalid ed25519 signature hex"))); __m.insert(Cow::Borrowed("manifest"), SVal::Null); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(true)); __m.insert(Cow::Borrowed("error"), SVal::Str(Cow::Borrowed(""))); __m.insert(Cow::Borrowed("manifest"), { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("id"), id.clone()); __m.insert(Cow::Borrowed("version"), version.clone()); __m.insert(Cow::Borrowed("scope"), scope.clone()); __m.insert(Cow::Borrowed("install_path"), install_path.clone()); __m.insert(Cow::Borrowed("sha256"), __st_lower(&(__st_str(&sha256)))); __m.insert(Cow::Borrowed("signature"), (if (&signature != &(SVal::Null)) { __st_lower(&(__st_str(&signature))) } else { SVal::Str(Cow::Borrowed("")) })); __m.insert(Cow::Borrowed("file_count"), __st_len(&files)); __m.insert(Cow::Borrowed("files"), files.clone()); SVal::Map(Rc::new(RefCell::new(__m))) }); SVal::Map(Rc::new(RefCell::new(__m))) };
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_build_package_manifest(__a: Vec<SVal>) -> SVal { build_package_manifest(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null), __a.get(2).cloned().unwrap_or(SVal::Null), __a.get(3).cloned().unwrap_or(SVal::Null), __a.get(4).cloned().unwrap_or(SVal::Null), __a.get(5).cloned().unwrap_or(SVal::Null), __a.get(6).cloned().unwrap_or(SVal::Null)) }

pub fn publish_catalogue_entry(mut catalogue: SVal, mut id: SVal, mut entry: SVal) -> SVal {
    let mut chk = validate_catalogue_entry(id.clone(), entry.clone());
    if (!(&((&chk).get_key("ok"))).is_truthy()) {
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(false)); __m.insert(Cow::Borrowed("error"), (&chk).get_key("reason")); __m.insert(Cow::Borrowed("catalogue"), catalogue.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    if (&(__st_has_key(&catalogue, &id))).is_truthy() {
        let mut prev = (&((&catalogue).get_ref(&id))).get_key("version");
        if ((&(compare_semver((&entry).get_key("version"), prev.clone()))).as_num() < (&(SVal::Num(0.000000))).as_num()) {
            return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(false)); __m.insert(Cow::Borrowed("error"), (((SVal::Str(Cow::Borrowed("cannot publish older version ")) + __st_str(&((&entry).get_key("version")))) + SVal::Str(Cow::Borrowed(" over "))) + __st_str(&prev))); __m.insert(Cow::Borrowed("catalogue"), catalogue.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
        }
    }
    (&catalogue).set(id.clone(), entry.clone());
    return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(true)); __m.insert(Cow::Borrowed("error"), SVal::Str(Cow::Borrowed(""))); __m.insert(Cow::Borrowed("catalogue"), catalogue.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_publish_catalogue_entry(__a: Vec<SVal>) -> SVal { publish_catalogue_entry(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null), __a.get(2).cloned().unwrap_or(SVal::Null)) }

pub fn validate_helper_request(mut req: SVal) -> SVal {
    let type_fn = SVal::Func(Rc::new(|args: Vec<SVal>| SVal::Str(Cow::Borrowed(match args.first() { Some(SVal::Null) | None => "null", Some(SVal::Bool(_)) => "boolean", Some(SVal::Num(_)) => "number", Some(SVal::Str(_)) => "string", Some(SVal::Arr(_)) => "array", Some(SVal::Map(_)) => "map", Some(SVal::Func(_)) => "function" }))));
    if ((&req == &(SVal::Null)) || (!(&((&(type_fn.clone())).call(vec![req.clone()]))).eq_str("map"))) {
        return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), SVal::Str(Cow::Borrowed("helper request must be a JSON object")))]))));
    }
    if (((!(&(__st_has_key(&req, &(SVal::Str(Cow::Borrowed("op")))))).is_truthy()) || (!(&(__st_has_key(&req, &(SVal::Str(Cow::Borrowed("component_id")))))).is_truthy())) || (!(&(__st_has_key(&req, &(SVal::Str(Cow::Borrowed("install_path")))))).is_truthy())) {
        return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), SVal::Str(Cow::Borrowed("missing required helper fields (op, component_id, install_path)")))]))));
    }
    let mut op = __st_str(&((&req).get_key("op")));
    if ((((!(&op).eq_str("install")) && (!(&op).eq_str("upgrade"))) && (!(&op).eq_str("downgrade"))) && (!(&op).eq_str("remove"))) {
        return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), (SVal::Str(Cow::Borrowed("unsupported helper op: ")) + op.clone()))]))));
    }
    let mut id = __st_str(&((&req).get_key("component_id")));
    if (!(&(is_valid_component_id(id.clone()))).is_truthy()) {
        return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), (SVal::Str(Cow::Borrowed("invalid component_id: ")) + id.clone()))]))));
    }
    let mut scope = (if ((&(__st_has_key(&req, &(SVal::Str(Cow::Borrowed("scope")))))).is_truthy() && (&((&req).get_key("scope")) != &(SVal::Null))) { __st_str(&((&req).get_key("scope"))) } else { SVal::Str(Cow::Borrowed("system")) });
    let mut pchk = validate_install_path(__st_str(&((&req).get_key("install_path"))), scope.clone());
    if (!(&((&pchk).get_key("ok"))).is_truthy()) {
        return pchk.clone();
    }
    if (!(&op).eq_str("remove")) {
        if ((!(&(__st_has_key(&req, &(SVal::Str(Cow::Borrowed("archive_path")))))).is_truthy()) || (&(__st_len(&(__st_trim(&(__st_str(&((&req).get_key("archive_path"))))))))).eq_num(0.000000)) {
            return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), (SVal::Str(Cow::Borrowed("archive_path required for ")) + op.clone()))]))));
        }
        if (((&(__st_has_key(&req, &(SVal::Str(Cow::Borrowed("sha256")))))).is_truthy() && (&((&req).get_key("sha256")) != &(SVal::Null))) && ((&(__st_len(&(__st_str(&((&req).get_key("sha256"))))))).as_num() > (&(SVal::Num(0.000000))).as_num())) {
            if (!(&(validate_sha256(__st_str(&((&req).get_key("sha256")))))).is_truthy()) {
                return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), SVal::Str(Cow::Borrowed("invalid sha256 in helper request")))]))));
            }
        }
    }
    return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(true)), (Cow::Borrowed("reason"), SVal::Str(Cow::Borrowed("")))]))));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_validate_helper_request(__a: Vec<SVal>) -> SVal { validate_helper_request(__a.get(0).cloned().unwrap_or(SVal::Null)) }

pub fn helper_check_dpkg_owner(mut target_path: SVal) -> SVal {
    let type_fn = SVal::Func(Rc::new(|args: Vec<SVal>| SVal::Str(Cow::Borrowed(match args.first() { Some(SVal::Null) | None => "null", Some(SVal::Bool(_)) => "boolean", Some(SVal::Num(_)) => "number", Some(SVal::Str(_)) => "string", Some(SVal::Arr(_)) => "array", Some(SVal::Map(_)) => "map", Some(SVal::Func(_)) => "function" }))));
    let mut res = __st_exec(&(SVal::Str(Cow::Borrowed("dpkg"))), Some(&(SVal::Arr(Rc::new(RefCell::new(vec![SVal::Str(Cow::Borrowed("-S")), target_path.clone()]))))));
    if ((((&res != &(SVal::Null)) && (&((&(type_fn.clone())).call(vec![res.clone()]))).eq_str("map")) && (&((&res).get_key("code"))).eq_num(0.000000)) && ((&(__st_len(&(__st_trim(&(__st_str(&((&res).get_key("stdout"))))))))).as_num() > (&(SVal::Num(0.000000))).as_num())) {
        return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(false)), (Cow::Borrowed("reason"), (SVal::Str(Cow::Borrowed("path owned by dpkg: ")) + __st_trim(&(__st_str(&((&res).get_key("stdout")))))))]))));
    }
    return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("ok"), SVal::Bool(true)), (Cow::Borrowed("reason"), SVal::Str(Cow::Borrowed("")))]))));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_helper_check_dpkg_owner(__a: Vec<SVal>) -> SVal { helper_check_dpkg_owner(__a.get(0).cloned().unwrap_or(SVal::Null)) }

pub fn helper_backup_path(mut backup_root: SVal, mut tx_id: SVal, mut component_id: SVal) -> SVal {
    let mut root = (if ((&backup_root != &(SVal::Null)) && ((&(__st_len(&(__st_trim(&(__st_str(&backup_root))))))).as_num() > (&(SVal::Num(0.000000))).as_num())) { __st_str(&backup_root) } else { SVal::Str(Cow::Borrowed("/var/lib/infinityos-updater/backups")) });
    let mut tx = (if ((&tx_id != &(SVal::Null)) && ((&(__st_len(&(__st_trim(&(__st_str(&tx_id))))))).as_num() > (&(SVal::Num(0.000000))).as_num())) { __st_str(&tx_id) } else { SVal::Str(Cow::Borrowed("tx-current")) });
    return ((((root.clone() + SVal::Str(Cow::Borrowed("/"))) + tx.clone()) + SVal::Str(Cow::Borrowed("/"))) + __st_str(&component_id));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_helper_backup_path(__a: Vec<SVal>) -> SVal { helper_backup_path(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null), __a.get(2).cloned().unwrap_or(SVal::Null)) }

pub fn helper_create_backup(mut dest: SVal, mut backup_path: SVal) -> SVal {
    let file_exists = SVal::Func(Rc::new(|args: Vec<SVal>| { let p = args.get(0).map(|a| a.to_string()).unwrap_or_default(); SVal::Bool(std::path::Path::new(&p).exists()) }));
    if (!(&((&file_exists).call(vec![dest.clone()]))).is_truthy()) {
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(true)); __m.insert(Cow::Borrowed("backed_up"), SVal::Bool(false)); __m.insert(Cow::Borrowed("backup_path"), SVal::Str(Cow::Borrowed(""))); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    let mut parent_idx = (__st_len(&backup_path) - SVal::Num(1.000000));
    while (((&parent_idx).as_num() > (&(SVal::Num(0.000000))).as_num()) && (!(&(__st_slice(&backup_path, &parent_idx, Some(&((parent_idx.clone() + SVal::Num(1.000000))))))).eq_str("/"))) {
        parent_idx = (parent_idx.clone() - SVal::Num(1.000000));
    }
    let mut parent_dir = __st_slice(&backup_path, &(SVal::Num(0.000000)), Some(&parent_idx));
    if ((&(__st_len(&parent_dir))).as_num() > (&(SVal::Num(0.000000))).as_num()) {
        __st_mkdir(&parent_dir);
    }
    if (&((&file_exists).call(vec![backup_path.clone()]))).is_truthy() {
        __st_remove_dir(&backup_path);
    }
    if (!(&(__st_rename(&dest, &backup_path))).is_truthy()) {
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(false)); __m.insert(Cow::Borrowed("backed_up"), SVal::Bool(false)); __m.insert(Cow::Borrowed("backup_path"), SVal::Str(Cow::Borrowed(""))); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(true)); __m.insert(Cow::Borrowed("backed_up"), SVal::Bool(true)); __m.insert(Cow::Borrowed("backup_path"), backup_path.clone()); SVal::Map(Rc::new(RefCell::new(__m))) };
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_helper_create_backup(__a: Vec<SVal>) -> SVal { helper_create_backup(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null)) }

pub fn helper_restore_backup(mut dest: SVal, mut backup_path: SVal) -> SVal {
    let file_exists = SVal::Func(Rc::new(|args: Vec<SVal>| { let p = args.get(0).map(|a| a.to_string()).unwrap_or_default(); SVal::Bool(std::path::Path::new(&p).exists()) }));
    if (&((&file_exists).call(vec![dest.clone()]))).is_truthy() {
        __st_remove_dir(&dest);
    }
    if (((&backup_path != &(SVal::Null)) && ((&(__st_len(&(__st_str(&backup_path))))).as_num() > (&(SVal::Num(0.000000))).as_num())) && (&((&file_exists).call(vec![__st_str(&backup_path)]))).is_truthy()) {
        if (!(&(__st_rename(&(__st_str(&backup_path)), &dest))).is_truthy()) {
            return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(false)); __m.insert(Cow::Borrowed("error"), (SVal::Str(Cow::Borrowed("failed to restore backup from ")) + __st_str(&backup_path))); SVal::Map(Rc::new(RefCell::new(__m))) };
        }
    }
    return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("ok"), SVal::Bool(true)); __m.insert(Cow::Borrowed("error"), SVal::Str(Cow::Borrowed(""))); SVal::Map(Rc::new(RefCell::new(__m))) };
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_helper_restore_backup(__a: Vec<SVal>) -> SVal { helper_restore_backup(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null)) }

pub fn helper_execute_request(mut req_json: SVal) -> SVal {
    let type_fn = SVal::Func(Rc::new(|args: Vec<SVal>| SVal::Str(Cow::Borrowed(match args.first() { Some(SVal::Null) | None => "null", Some(SVal::Bool(_)) => "boolean", Some(SVal::Num(_)) => "number", Some(SVal::Str(_)) => "string", Some(SVal::Arr(_)) => "array", Some(SVal::Map(_)) => "map", Some(SVal::Func(_)) => "function" }))));
    let mut req = __st_json_parse(&req_json);
    if ((((&req != &(SVal::Null)) && (&((&(type_fn.clone())).call(vec![req.clone()]))).eq_str("map")) && (&(__st_has_key(&req, &(SVal::Str(Cow::Borrowed("op")))))).is_truthy()) && (&((&req).get_key("op"))).eq_str("restore_backup")) {
        let mut dest = __st_str(&((&req).get_key("install_path")));
        let mut bp = __st_str(&((&req).get_key("backup_path")));
        let mut r = helper_restore_backup(dest.clone(), bp.clone());
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), (if (&((&r).get_key("ok"))).is_truthy() { SVal::Num(0.000000) } else { SVal::Num(1.000000) })); __m.insert(Cow::Borrowed("error"), (&r).get_key("error")); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    let mut chk = validate_helper_request(req.clone());
    if (!(&((&chk).get_key("ok"))).is_truthy()) {
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(2.000000)); __m.insert(Cow::Borrowed("error"), (SVal::Str(Cow::Borrowed("policy rejected request: ")) + (&chk).get_key("reason"))); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    let mut op = __st_str(&((&req).get_key("op")));
    let mut id = __st_str(&((&req).get_key("component_id")));
    let mut dest = __st_str(&((&req).get_key("install_path")));
    let mut dpkg_chk = helper_check_dpkg_owner(dest.clone());
    if (!(&((&dpkg_chk).get_key("ok"))).is_truthy()) {
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(3.000000)); __m.insert(Cow::Borrowed("error"), (&dpkg_chk).get_key("reason")); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    let mut b_root = (if (&(__st_has_key(&req, &(SVal::Str(Cow::Borrowed("backup_root")))))).is_truthy() { (&req).get_key("backup_root") } else { SVal::Str(Cow::Borrowed("")) });
    let mut tx_id = (if (&(__st_has_key(&req, &(SVal::Str(Cow::Borrowed("tx_id")))))).is_truthy() { (&req).get_key("tx_id") } else { SVal::Str(Cow::Borrowed("tx-current")) });
    let mut b_path = helper_backup_path(b_root.clone(), tx_id.clone(), id.clone());
    if (&op).eq_str("remove") {
        let mut b = helper_create_backup(dest.clone(), b_path.clone());
        if (!(&((&b).get_key("ok"))).is_truthy()) {
            return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(1.000000)); __m.insert(Cow::Borrowed("error"), ((SVal::Str(Cow::Borrowed("failed to back up ")) + dest.clone()) + SVal::Str(Cow::Borrowed(" before remove")))); SVal::Map(Rc::new(RefCell::new(__m))) };
        }
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(0.000000)); __m.insert(Cow::Borrowed("error"), SVal::Str(Cow::Borrowed(""))); __m.insert(Cow::Borrowed("backup_path"), (&b).get_key("backup_path")); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    let mut archive = __st_str(&((&req).get_key("archive_path")));
    if (((&(__st_has_key(&req, &(SVal::Str(Cow::Borrowed("sha256")))))).is_truthy() && (&((&req).get_key("sha256")) != &(SVal::Null))) && ((&(__st_len(&(__st_str(&((&req).get_key("sha256"))))))).as_num() > (&(SVal::Num(0.000000))).as_num())) {
        let mut actual = __st_sha256_file(&archive);
        if (&(__st_lower(&actual)) != &(__st_lower(&(__st_str(&((&req).get_key("sha256"))))))) {
            return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(4.000000)); __m.insert(Cow::Borrowed("error"), (((SVal::Str(Cow::Borrowed("sha256 mismatch: expected ")) + __st_str(&((&req).get_key("sha256")))) + SVal::Str(Cow::Borrowed(", got "))) + actual.clone())); SVal::Map(Rc::new(RefCell::new(__m))) };
        }
    }
    let mut b = helper_create_backup(dest.clone(), b_path.clone());
    if (!(&((&b).get_key("ok"))).is_truthy()) {
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(1.000000)); __m.insert(Cow::Borrowed("error"), (SVal::Str(Cow::Borrowed("failed to create on-disk backup for ")) + dest.clone())); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    if (!(&(__st_mkdir(&dest))).is_truthy()) {
        helper_restore_backup(dest.clone(), (&b).get_key("backup_path"));
        return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(1.000000)); __m.insert(Cow::Borrowed("error"), (SVal::Str(Cow::Borrowed("failed to create destination dir ")) + dest.clone())); SVal::Map(Rc::new(RefCell::new(__m))) };
    }
    return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(0.000000)); __m.insert(Cow::Borrowed("error"), SVal::Str(Cow::Borrowed(""))); __m.insert(Cow::Borrowed("backup_path"), (&b).get_key("backup_path")); __m.insert(Cow::Borrowed("extract"), SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("archive"), archive.clone()), (Cow::Borrowed("dest"), dest.clone())]))))); SVal::Map(Rc::new(RefCell::new(__m))) };
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_helper_execute_request(__a: Vec<SVal>) -> SVal { helper_execute_request(__a.get(0).cloned().unwrap_or(SVal::Null)) }

pub fn runner_atomic_write(mut path: SVal, mut content: SVal) -> SVal {
    let write_file = SVal::Func(Rc::new(|args: Vec<SVal>| { let p = args.get(0).map(|a| a.to_string()).unwrap_or_default(); let c = args.get(1).map(|a| a.to_string()).unwrap_or_default(); SVal::Bool(std::fs::write(&p, c).is_ok()) }));
    let mut tmp = (path.clone() + SVal::Str(Cow::Borrowed(".tmp")));
    if (!(&((&write_file).call(vec![tmp.clone(), content.clone()]))).is_truthy()) {
        return SVal::Bool(false);
    }
    return __st_rename(&tmp, &path);
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_runner_atomic_write(__a: Vec<SVal>) -> SVal { runner_atomic_write(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null)) }

pub fn runner_cli_main() -> SVal {
    let type_fn = SVal::Func(Rc::new(|args: Vec<SVal>| SVal::Str(Cow::Borrowed(match args.first() { Some(SVal::Null) | None => "null", Some(SVal::Bool(_)) => "boolean", Some(SVal::Num(_)) => "number", Some(SVal::Str(_)) => "string", Some(SVal::Arr(_)) => "array", Some(SVal::Map(_)) => "map", Some(SVal::Func(_)) => "function" }))));
    let read_file = SVal::Func(Rc::new(|args: Vec<SVal>| { let p = args.get(0).map(|a| a.to_string()).unwrap_or_default(); SVal::Str(Cow::Owned(std::fs::read_to_string(&p).unwrap_or_default())) }));
    let file_exists = SVal::Func(Rc::new(|args: Vec<SVal>| { let p = args.get(0).map(|a| a.to_string()).unwrap_or_default(); SVal::Bool(std::path::Path::new(&p).exists()) }));
    let env = SVal::Func(Rc::new(|args: Vec<SVal>| { let k = args.get(0).map(|a| a.to_string()).unwrap_or_default(); match std::env::var(&k) { Ok(v) => SVal::Str(Cow::Owned(v)), Err(_) => SVal::Null } }));
    let clock = SVal::Func(Rc::new(|_args: Vec<SVal>| { let s = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs_f64()).unwrap_or(0.0); SVal::Num(s) }));
    let mut state_path = (&env).call(vec![SVal::Str(Cow::Borrowed("INFINITY_STATE_PATH"))]);
    if ((&state_path == &(SVal::Null)) || (&(__st_len(&(__st_str(&state_path))))).eq_num(0.000000)) {
        state_path = SVal::Str(Cow::Borrowed("infinity-state.json"));
    }
    let mut cat_path = (&env).call(vec![SVal::Str(Cow::Borrowed("INFINITY_CATALOGUE_PATH"))]);
    if ((&cat_path == &(SVal::Null)) || (&(__st_len(&(__st_str(&cat_path))))).eq_num(0.000000)) {
        cat_path = SVal::Str(Cow::Borrowed("catalogue.json"));
    }
    let mut state = db_load_from_path(state_path.clone());
    let mut catalogue = SVal::Map(Rc::new(RefCell::new(SMap::new())));
    if (&((&file_exists).call(vec![cat_path.clone()]))).is_truthy() {
        let mut raw_cat = (&read_file).call(vec![cat_path.clone()]);
        if ((&(__st_len(&(__st_trim(&raw_cat))))).as_num() > (&(SVal::Num(0.000000))).as_num()) {
            let mut parsed = __st_json_parse(&raw_cat);
            if ((&parsed != &(SVal::Null)) && (&((&(type_fn.clone())).call(vec![parsed.clone()]))).eq_str("map")) {
                catalogue = parsed.clone();
            }
        }
    }
    let mut argv = __st_args();
    let mut ts = SVal::Num((&((&clock).call(vec![]))).as_num().trunc());
    let mut outcome = cli_dispatch(argv.clone(), state.clone(), catalogue.clone(), ts.clone());
    if ((&(__st_len(&(__st_str(&((&outcome).get_key("output"))))))).as_num() > (&(SVal::Num(0.000000))).as_num()) {
        { let __pa: [&SVal; 1] = [&((&outcome).get_key("output"))]; println!("{}", __pa.iter().map(|a| a.to_string()).collect::<Vec<_>>().join(" ")); SVal::Null };
    }
    if (&((&outcome).get_key("save_state"))).is_truthy() {
        if (!(&(runner_atomic_write(state_path.clone(), __st_json_stringify(&((&outcome).get_key("state")))))).is_truthy()) {
            { let __pa: [&SVal; 1] = [&((SVal::Str(Cow::Borrowed("Error saving state to ")) + state_path.clone()))]; println!("{}", __pa.iter().map(|a| a.to_string()).collect::<Vec<_>>().join(" ")); SVal::Null };
            return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), SVal::Num(1.000000)); __m.insert(Cow::Borrowed("actions"), SVal::Arr(Rc::new(RefCell::new(vec![])))); SVal::Map(Rc::new(RefCell::new(__m))) };
        }
    }
    return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("exit_code"), (&outcome).get_key("exit_code")); __m.insert(Cow::Borrowed("actions"), (&outcome).get_key("actions")); SVal::Map(Rc::new(RefCell::new(__m))) };
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_runner_cli_main(__a: Vec<SVal>) -> SVal { runner_cli_main() }

pub fn runner_publish_catalogue(mut cat_path: SVal, mut id: SVal, mut entry_path: SVal) -> SVal {
    let type_fn = SVal::Func(Rc::new(|args: Vec<SVal>| SVal::Str(Cow::Borrowed(match args.first() { Some(SVal::Null) | None => "null", Some(SVal::Bool(_)) => "boolean", Some(SVal::Num(_)) => "number", Some(SVal::Str(_)) => "string", Some(SVal::Arr(_)) => "array", Some(SVal::Map(_)) => "map", Some(SVal::Func(_)) => "function" }))));
    let read_file = SVal::Func(Rc::new(|args: Vec<SVal>| { let p = args.get(0).map(|a| a.to_string()).unwrap_or_default(); SVal::Str(Cow::Owned(std::fs::read_to_string(&p).unwrap_or_default())) }));
    let file_exists = SVal::Func(Rc::new(|args: Vec<SVal>| { let p = args.get(0).map(|a| a.to_string()).unwrap_or_default(); SVal::Bool(std::path::Path::new(&p).exists()) }));
    if (!(&((&file_exists).call(vec![entry_path.clone()]))).is_truthy()) {
        return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("exit_code"), SVal::Num(1.000000)), (Cow::Borrowed("output"), (SVal::Str(Cow::Borrowed("Error: entry file not found: ")) + entry_path.clone()))]))));
    }
    let mut entry = __st_json_parse(&((&read_file).call(vec![entry_path.clone()])));
    let mut cat = SVal::Map(Rc::new(RefCell::new(SMap::new())));
    if (&((&file_exists).call(vec![cat_path.clone()]))).is_truthy() {
        let mut raw = (&read_file).call(vec![cat_path.clone()]);
        if ((&(__st_len(&(__st_trim(&raw))))).as_num() > (&(SVal::Num(0.000000))).as_num()) {
            let mut parsed = __st_json_parse(&raw);
            if ((&parsed != &(SVal::Null)) && (&((&(type_fn.clone())).call(vec![parsed.clone()]))).eq_str("map")) {
                cat = parsed.clone();
            }
        }
    }
    let mut res = publish_catalogue_entry(cat.clone(), id.clone(), entry.clone());
    if (!(&((&res).get_key("ok"))).is_truthy()) {
        return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("exit_code"), SVal::Num(1.000000)), (Cow::Borrowed("output"), (SVal::Str(Cow::Borrowed("publish rejected: ")) + (&res).get_key("error")))]))));
    }
    if (!(&(runner_atomic_write(cat_path.clone(), __st_json_stringify(&((&res).get_key("catalogue")))))).is_truthy()) {
        return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("exit_code"), SVal::Num(1.000000)), (Cow::Borrowed("output"), (SVal::Str(Cow::Borrowed("Error writing catalogue to ")) + cat_path.clone()))]))));
    }
    return SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("exit_code"), SVal::Num(0.000000)), (Cow::Borrowed("output"), (((SVal::Str(Cow::Borrowed("Published ")) + id.clone()) + SVal::Str(Cow::Borrowed(" to "))) + cat_path.clone()))]))));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_runner_publish_catalogue(__a: Vec<SVal>) -> SVal { runner_publish_catalogue(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null), __a.get(2).cloned().unwrap_or(SVal::Null)) }

pub fn test_sample_catalogue() -> SVal {
    let mut sha_a = SVal::Str(Cow::Borrowed("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    let mut sha_b = SVal::Str(Cow::Borrowed("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
    return { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("origin-core"), { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("version"), SVal::Str(Cow::Borrowed("1.1.0"))); __m.insert(Cow::Borrowed("install_path"), SVal::Str(Cow::Borrowed("/usr/share/gnome-shell/extensions/Origin@appscreen"))); __m.insert(Cow::Borrowed("scope"), SVal::Str(Cow::Borrowed("system"))); __m.insert(Cow::Borrowed("description"), SVal::Str(Cow::Borrowed("Origin shell core"))); __m.insert(Cow::Borrowed("channel"), SVal::Str(Cow::Borrowed("stable"))); __m.insert(Cow::Borrowed("url"), SVal::Str(Cow::Borrowed("https://updates.infinityos.uk/origin-core-1.1.0.tar.xz"))); __m.insert(Cow::Borrowed("sha256"), sha_a.clone()); __m.insert(Cow::Borrowed("depends"), SVal::Arr(Rc::new(RefCell::new(vec![])))); SVal::Map(Rc::new(RefCell::new(__m))) }); __m.insert(Cow::Borrowed("origin-cc"), { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("version"), SVal::Str(Cow::Borrowed("1.0.0"))); __m.insert(Cow::Borrowed("install_path"), SVal::Str(Cow::Borrowed("/usr/share/gnome-shell/extensions/OriginUICC@quandz24.extension"))); __m.insert(Cow::Borrowed("scope"), SVal::Str(Cow::Borrowed("system"))); __m.insert(Cow::Borrowed("description"), SVal::Str(Cow::Borrowed("Origin control center"))); __m.insert(Cow::Borrowed("channel"), SVal::Str(Cow::Borrowed("stable"))); __m.insert(Cow::Borrowed("url"), SVal::Str(Cow::Borrowed("https://updates.infinityos.uk/origin-cc-1.0.0.tar.xz"))); __m.insert(Cow::Borrowed("sha256"), sha_b.clone()); __m.insert(Cow::Borrowed("depends"), SVal::Arr(Rc::new(RefCell::new(vec![SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("id"), SVal::Str(Cow::Borrowed("origin-core"))), (Cow::Borrowed("min_version"), SVal::Str(Cow::Borrowed(">=1.1.0")))]))))])))); SVal::Map(Rc::new(RefCell::new(__m))) }); SVal::Map(Rc::new(RefCell::new(__m))) };
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_test_sample_catalogue(__a: Vec<SVal>) -> SVal { test_sample_catalogue() }

pub fn test_semver_suite() -> SVal {
    let assert = SVal::Func(Rc::new(|args: Vec<SVal>| { if !args.first().map(|a| a.is_truthy()).unwrap_or(false) { panic!("{}", args.get(1).map(|m| m.to_string()).unwrap_or_else(|| "Assertion failed".to_string())); } SVal::Bool(true) }));
    (&assert).call(vec![SVal::Bool((&(compare_semver(SVal::Str(Cow::Borrowed("1.2.3")), SVal::Str(Cow::Borrowed("1.2.3"))))).eq_num(0.000000)), SVal::Str(Cow::Borrowed("semver eq"))]);
    (&assert).call(vec![SVal::Bool((&(compare_semver(SVal::Str(Cow::Borrowed("1.2.3")), SVal::Str(Cow::Borrowed("1.2.4"))))).eq_num(-1.000000)), SVal::Str(Cow::Borrowed("semver lt"))]);
    (&assert).call(vec![SVal::Bool((&(compare_semver(SVal::Str(Cow::Borrowed("2.0.0")), SVal::Str(Cow::Borrowed("1.9.9"))))).eq_num(1.000000)), SVal::Str(Cow::Borrowed("semver gt"))]);
    (&assert).call(vec![SVal::Bool((&(compare_semver(SVal::Str(Cow::Borrowed("1.0.0-beta.2")), SVal::Str(Cow::Borrowed("1.0.0"))))).eq_num(-1.000000)), SVal::Str(Cow::Borrowed("semver prerelease"))]);
    (&assert).call(vec![SVal::Bool((&(compare_semver(SVal::Str(Cow::Borrowed("v1.0.0")), SVal::Str(Cow::Borrowed("1.0.0"))))).eq_num(0.000000)), SVal::Str(Cow::Borrowed("semver v-prefix"))]);
    (&assert).call(vec![satisfies_constraint(SVal::Str(Cow::Borrowed("1.4.0")), SVal::Str(Cow::Borrowed("^1.2.0"))), SVal::Str(Cow::Borrowed("caret ok"))]);
    (&assert).call(vec![SVal::Bool(!(&(satisfies_constraint(SVal::Str(Cow::Borrowed("2.0.0")), SVal::Str(Cow::Borrowed("^1.2.0"))))).is_truthy()), SVal::Str(Cow::Borrowed("caret major reject"))]);
    (&assert).call(vec![satisfies_constraint(SVal::Str(Cow::Borrowed("1.2.9")), SVal::Str(Cow::Borrowed("~1.2.3"))), SVal::Str(Cow::Borrowed("tilde ok"))]);
    (&assert).call(vec![SVal::Bool(!(&(satisfies_constraint(SVal::Str(Cow::Borrowed("1.3.0")), SVal::Str(Cow::Borrowed("~1.2.3"))))).is_truthy()), SVal::Str(Cow::Borrowed("tilde minor reject"))]);
    (&assert).call(vec![SVal::Bool((&(classify_transition(SVal::Str(Cow::Borrowed("")), SVal::Str(Cow::Borrowed("1.0.0"))))).eq_str("install")), SVal::Str(Cow::Borrowed("classify install"))]);
    (&assert).call(vec![SVal::Bool((&(classify_transition(SVal::Str(Cow::Borrowed("1.0.0")), SVal::Str(Cow::Borrowed("1.1.0"))))).eq_str("upgrade")), SVal::Str(Cow::Borrowed("classify upgrade"))]);
    (&assert).call(vec![SVal::Bool((&(classify_transition(SVal::Str(Cow::Borrowed("1.1.0")), SVal::Str(Cow::Borrowed("1.0.0"))))).eq_str("downgrade")), SVal::Str(Cow::Borrowed("classify downgrade"))]);
    (&assert).call(vec![SVal::Bool((&(classify_transition(SVal::Str(Cow::Borrowed("1.0.0")), SVal::Str(Cow::Borrowed("1.0.0"))))).eq_str("noop")), SVal::Str(Cow::Borrowed("classify noop"))]);
    return SVal::Bool(true);
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_test_semver_suite(__a: Vec<SVal>) -> SVal { test_semver_suite() }

pub fn test_security_policy_suite() -> SVal {
    let zip = SVal::Func(Rc::new(|args: Vec<SVal>| { let mut out: Vec<SVal> = Vec::new(); if let (Some(SVal::Arr(a)), Some(SVal::Arr(b))) = (args.get(0), args.get(1)) { for (x, y) in a.borrow().iter().zip(b.borrow().iter()) { out.push(SVal::Arr(Rc::new(RefCell::new(vec![x.clone(), y.clone()])))); } } SVal::Arr(Rc::new(RefCell::new(out))) }));
    let assert = SVal::Func(Rc::new(|args: Vec<SVal>| { if !args.first().map(|a| a.is_truthy()).unwrap_or(false) { panic!("{}", args.get(1).map(|m| m.to_string()).unwrap_or_else(|| "Assertion failed".to_string())); } SVal::Bool(true) }));
    let mut ok = validate_install_path(SVal::Str(Cow::Borrowed("/usr/share/icons/Radience")), SVal::Str(Cow::Borrowed("system")));
    (&assert).call(vec![(&ok).get_key("ok"), SVal::Str(Cow::Borrowed("valid system icon path"))]);
    let mut bad_etc = validate_install_path(SVal::Str(Cow::Borrowed("/etc/shadow")), SVal::Str(Cow::Borrowed("system")));
    (&assert).call(vec![SVal::Bool(!(&((&bad_etc).get_key("ok"))).is_truthy()), SVal::Str(Cow::Borrowed("block /etc"))]);
    let mut bad_trav = validate_install_path(SVal::Str(Cow::Borrowed("/usr/share/icons/../../etc/passwd")), SVal::Str(Cow::Borrowed("system")));
    (&assert).call(vec![SVal::Bool(!(&((&bad_trav).get_key("ok"))).is_truthy()), SVal::Str(Cow::Borrowed("block traversal"))]);
    let mut bad_slash = validate_install_path(SVal::Str(Cow::Borrowed("/usr/share//icons/Radience")), SVal::Str(Cow::Borrowed("system")));
    (&assert).call(vec![SVal::Bool(!(&((&bad_slash).get_key("ok"))).is_truthy()), SVal::Str(Cow::Borrowed("block duplicate slash"))]);
    let mut user_ok = validate_install_path(SVal::Str(Cow::Borrowed("~/.local/share/icons/Radience")), SVal::Str(Cow::Borrowed("user")));
    (&assert).call(vec![(&user_ok).get_key("ok"), SVal::Str(Cow::Borrowed("valid user path"))]);
    let mut zip_ok = validate_archive_manifest(SVal::Arr(Rc::new(RefCell::new(vec![SVal::Str(Cow::Borrowed("usr/file.txt")), SVal::Str(Cow::Borrowed("meta/info.json"))]))));
    (&assert).call(vec![(&zip_ok).get_key("ok"), SVal::Str(Cow::Borrowed("valid archive entries"))]);
    let mut zip_bad = validate_archive_manifest(SVal::Arr(Rc::new(RefCell::new(vec![SVal::Str(Cow::Borrowed("ok/file.txt")), SVal::Str(Cow::Borrowed("../escape"))]))));
    (&assert).call(vec![SVal::Bool(!(&((&zip_bad).get_key("ok"))).is_truthy()), SVal::Str(Cow::Borrowed("block zip-slip"))]);
    (&assert).call(vec![validate_sha256(__st_sha256(&(SVal::Str(Cow::Borrowed("infinityos"))))), SVal::Str(Cow::Borrowed("valid sha256 output"))]);
    return SVal::Bool(true);
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_test_security_policy_suite(__a: Vec<SVal>) -> SVal { test_security_policy_suite() }

pub fn test_solver_and_reconciler_suite() -> SVal {
    let assert = SVal::Func(Rc::new(|args: Vec<SVal>| { if !args.first().map(|a| a.is_truthy()).unwrap_or(false) { panic!("{}", args.get(1).map(|m| m.to_string()).unwrap_or_else(|| "Assertion failed".to_string())); } SVal::Bool(true) }));
    let mut cat = test_sample_catalogue();
    let mut cat_chk = validate_catalogue(cat.clone());
    (&assert).call(vec![SVal::Bool(((&((&cat_chk).get_key("ok"))).is_truthy() && (&((&cat_chk).get_key("count"))).eq_num(2.000000))), SVal::Str(Cow::Borrowed("catalogue valid"))]);
    let mut plan = plan_reconcile(SVal::Map(Rc::new(RefCell::new(SMap::new()))), cat.clone(), SVal::Arr(Rc::new(RefCell::new(vec![SVal::Str(Cow::Borrowed("origin-cc"))]))));
    (&assert).call(vec![(&plan).get_key("ok"), SVal::Str(Cow::Borrowed("reconcile plan ok"))]);
    (&assert).call(vec![SVal::Bool((&(__st_len(&((&plan).get_key("actions"))))).eq_num(2.000000)), SVal::Str(Cow::Borrowed("reconcile installs dep first"))]);
    (&assert).call(vec![SVal::Bool((&((&((&((&plan).get_key("actions"))).get_ref(&(SVal::Num(0.000000))))).get_key("id"))).eq_str("origin-core")), SVal::Str(Cow::Borrowed("dep ordered first"))]);
    (&assert).call(vec![SVal::Bool((&((&((&((&plan).get_key("actions"))).get_ref(&(SVal::Num(1.000000))))).get_key("id"))).eq_str("origin-cc")), SVal::Str(Cow::Borrowed("target ordered second"))]);
    let mut cyclic = SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("a"), { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("version"), SVal::Str(Cow::Borrowed("1.0.0"))); __m.insert(Cow::Borrowed("install_path"), SVal::Str(Cow::Borrowed("/usr/share/icons/A"))); __m.insert(Cow::Borrowed("scope"), SVal::Str(Cow::Borrowed("system"))); __m.insert(Cow::Borrowed("depends"), SVal::Arr(Rc::new(RefCell::new(vec![SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("id"), SVal::Str(Cow::Borrowed("b")))]))))])))); SVal::Map(Rc::new(RefCell::new(__m))) }), (Cow::Borrowed("b"), { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("version"), SVal::Str(Cow::Borrowed("1.0.0"))); __m.insert(Cow::Borrowed("install_path"), SVal::Str(Cow::Borrowed("/usr/share/icons/B"))); __m.insert(Cow::Borrowed("scope"), SVal::Str(Cow::Borrowed("system"))); __m.insert(Cow::Borrowed("depends"), SVal::Arr(Rc::new(RefCell::new(vec![SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("id"), SVal::Str(Cow::Borrowed("a")))]))))])))); SVal::Map(Rc::new(RefCell::new(__m))) })]))));
    let mut cyc_res = resolve_install_order(cyclic.clone(), SVal::Arr(Rc::new(RefCell::new(vec![SVal::Str(Cow::Borrowed("a"))]))));
    (&assert).call(vec![SVal::Bool(((!(&((&cyc_res).get_key("ok"))).is_truthy()) && (&(__st_contains(&((&cyc_res).get_key("error")), &(SVal::Str(Cow::Borrowed("cycle")))))).is_truthy())), SVal::Str(Cow::Borrowed("cycle detected"))]);
    let mut installed = { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("origin-core"), SVal::Str(Cow::Borrowed("1.1.0"))); __m.insert(Cow::Borrowed("origin-cc"), SVal::Str(Cow::Borrowed("1.0.0"))); SVal::Map(Rc::new(RefCell::new(__m))) };
    let mut snap = SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("origin-core"), SVal::Str(Cow::Borrowed("1.0.0")))]))));
    let mut sp = plan_system_snapshot_transition(installed.clone(), cat.clone(), snap.clone());
    (&assert).call(vec![SVal::Bool(((&((&sp).get_key("ok"))).is_truthy() && (&(__st_len(&((&sp).get_key("actions"))))).eq_num(2.000000))), SVal::Str(Cow::Borrowed("snapshot transition count"))]);
    (&assert).call(vec![SVal::Bool(((&((&((&((&sp).get_key("actions"))).get_ref(&(SVal::Num(0.000000))))).get_key("id"))).eq_str("origin-cc") && (&((&((&((&sp).get_key("actions"))).get_ref(&(SVal::Num(0.000000))))).get_key("action"))).eq_str("remove"))), SVal::Str(Cow::Borrowed("snapshot removes newer"))]);
    (&assert).call(vec![SVal::Bool(((&((&((&((&sp).get_key("actions"))).get_ref(&(SVal::Num(1.000000))))).get_key("id"))).eq_str("origin-core") && (&((&((&((&sp).get_key("actions"))).get_ref(&(SVal::Num(1.000000))))).get_key("action"))).eq_str("downgrade"))), SVal::Str(Cow::Borrowed("snapshot downgrades"))]);
    return SVal::Bool(true);
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_test_solver_and_reconciler_suite(__a: Vec<SVal>) -> SVal { test_solver_and_reconciler_suite() }

pub fn test_cli_db_and_ui_suite() -> SVal {
    let assert = SVal::Func(Rc::new(|args: Vec<SVal>| { if !args.first().map(|a| a.is_truthy()).unwrap_or(false) { panic!("{}", args.get(1).map(|m| m.to_string()).unwrap_or_else(|| "Assertion failed".to_string())); } SVal::Bool(true) }));
    let mut cat = test_sample_catalogue();
    let mut st = db_default_state();
    let mut res = cli_dispatch(SVal::Arr(Rc::new(RefCell::new(vec![SVal::Str(Cow::Borrowed("install")), SVal::Str(Cow::Borrowed("origin-cc"))]))), st.clone(), cat.clone(), SVal::Num(1700000000.000000));
    (&assert).call(vec![SVal::Bool((((&((&res).get_key("exit_code"))).eq_num(0.000000) && (&(__st_len(&((&res).get_key("actions"))))).eq_num(2.000000)) && (&((&res).get_key("save_state"))).is_truthy())), SVal::Str(Cow::Borrowed("cli install"))]);
    let mut rb = cli_dispatch(SVal::Arr(Rc::new(RefCell::new(vec![SVal::Str(Cow::Borrowed("rollback"))]))), (&res).get_key("state"), cat.clone(), SVal::Num(1700000010.000000));
    (&assert).call(vec![SVal::Bool((&((&rb).get_key("exit_code"))).eq_num(0.000000)), SVal::Str(Cow::Borrowed("cli rollback"))]);
    let mut list_after = cli_dispatch(SVal::Arr(Rc::new(RefCell::new(vec![SVal::Str(Cow::Borrowed("list")), SVal::Str(Cow::Borrowed("--json"))]))), (&rb).get_key("state"), cat.clone(), SVal::Num(1700000020.000000));
    (&assert).call(vec![SVal::Bool((&((&list_after).get_key("output"))).eq_str("{}")), SVal::Str(Cow::Borrowed("rollback cleared installed"))]);
    let mut ui0 = ui_initial_state();
    let mut ui1 = ui_reduce(ui0.clone(), { let mut __m = SMap::new(); __m.insert(Cow::Borrowed("type"), SVal::Str(Cow::Borrowed("toggle_select"))); __m.insert(Cow::Borrowed("id"), SVal::Str(Cow::Borrowed("origin-cc"))); SVal::Map(Rc::new(RefCell::new(__m))) }, db_default_state(), cat.clone());
    let mut ui2 = ui_reduce(ui1.clone(), SVal::Map(Rc::new(RefCell::new(SMap::from_sorted(vec![(Cow::Borrowed("type"), SVal::Str(Cow::Borrowed("preview_plan")))])))), db_default_state(), cat.clone());
    (&assert).call(vec![SVal::Bool((&((&ui2).get_key("banner_level"))).eq_str("success")), SVal::Str(Cow::Borrowed("ui preview plan success"))]);
    return SVal::Bool(true);
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_test_cli_db_and_ui_suite(__a: Vec<SVal>) -> SVal { test_cli_db_and_ui_suite() }

pub fn test_helper_policy_suite() -> SVal {
    let assert = SVal::Func(Rc::new(|args: Vec<SVal>| { if !args.first().map(|a| a.is_truthy()).unwrap_or(false) { panic!("{}", args.get(1).map(|m| m.to_string()).unwrap_or_else(|| "Assertion failed".to_string())); } SVal::Bool(true) }));
    let mut bad = helper_execute_request(__st_json_stringify(&({ let mut __m = SMap::new(); __m.insert(Cow::Borrowed("op"), SVal::Str(Cow::Borrowed("install"))); __m.insert(Cow::Borrowed("component_id"), SVal::Str(Cow::Borrowed("origin-core"))); __m.insert(Cow::Borrowed("scope"), SVal::Str(Cow::Borrowed("system"))); __m.insert(Cow::Borrowed("install_path"), SVal::Str(Cow::Borrowed("/etc/passwd"))); __m.insert(Cow::Borrowed("archive_path"), SVal::Str(Cow::Borrowed("x.tar.xz"))); SVal::Map(Rc::new(RefCell::new(__m))) })));
    (&assert).call(vec![SVal::Bool((&((&bad).get_key("exit_code"))).eq_num(2.000000)), SVal::Str(Cow::Borrowed("helper rejects /etc/passwd"))]);
    return SVal::Bool(true);
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_test_helper_policy_suite(__a: Vec<SVal>) -> SVal { test_helper_policy_suite() }

pub fn run_all_steel_self_tests() -> SVal {
    let assert = SVal::Func(Rc::new(|args: Vec<SVal>| { if !args.first().map(|a| a.is_truthy()).unwrap_or(false) { panic!("{}", args.get(1).map(|m| m.to_string()).unwrap_or_else(|| "Assertion failed".to_string())); } SVal::Bool(true) }));
    (&assert).call(vec![test_semver_suite(), SVal::Str(Cow::Borrowed("semver suite"))]);
    (&assert).call(vec![test_security_policy_suite(), SVal::Str(Cow::Borrowed("security suite"))]);
    (&assert).call(vec![test_solver_and_reconciler_suite(), SVal::Str(Cow::Borrowed("solver suite"))]);
    (&assert).call(vec![test_cli_db_and_ui_suite(), SVal::Str(Cow::Borrowed("cli/db/ui suite"))]);
    (&assert).call(vec![test_helper_policy_suite(), SVal::Str(Cow::Borrowed("helper suite"))]);
    return SVal::Bool(true);
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_run_all_steel_self_tests(__a: Vec<SVal>) -> SVal { run_all_steel_self_tests() }

pub fn steel_compare_semver(mut a: SVal, mut b: SVal) -> SVal {
    return compare_semver(a.clone(), b.clone());
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_steel_compare_semver(__a: Vec<SVal>) -> SVal { steel_compare_semver(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null)) }

pub fn steel_satisfies_constraint(mut ver: SVal, mut constraint: SVal) -> SVal {
    return satisfies_constraint(ver.clone(), constraint.clone());
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_steel_satisfies_constraint(__a: Vec<SVal>) -> SVal { steel_satisfies_constraint(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null)) }

pub fn steel_validate_path_json(mut path: SVal, mut scope: SVal) -> SVal {
    return __st_json_stringify(&(validate_install_path(path.clone(), scope.clone())));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_steel_validate_path_json(__a: Vec<SVal>) -> SVal { steel_validate_path_json(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null)) }

pub fn steel_validate_archive_json(mut entries_json: SVal) -> SVal {
    let mut entries = __st_json_parse(&entries_json);
    return __st_json_stringify(&(validate_archive_manifest(entries.clone())));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_steel_validate_archive_json(__a: Vec<SVal>) -> SVal { steel_validate_archive_json(__a.get(0).cloned().unwrap_or(SVal::Null)) }

pub fn steel_validate_catalogue_json(mut catalogue_json: SVal) -> SVal {
    let mut cat = __st_json_parse(&catalogue_json);
    return __st_json_stringify(&(validate_catalogue(cat.clone())));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_steel_validate_catalogue_json(__a: Vec<SVal>) -> SVal { steel_validate_catalogue_json(__a.get(0).cloned().unwrap_or(SVal::Null)) }

pub fn steel_plan_reconcile(mut installed_json: SVal, mut catalogue_json: SVal, mut targets_json: SVal) -> SVal {
    let mut installed = __st_json_parse(&installed_json);
    let mut catalogue = __st_json_parse(&catalogue_json);
    let mut targets = __st_json_parse(&targets_json);
    return __st_json_stringify(&(plan_reconcile(installed.clone(), catalogue.clone(), targets.clone())));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_steel_plan_reconcile(__a: Vec<SVal>) -> SVal { steel_plan_reconcile(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null), __a.get(2).cloned().unwrap_or(SVal::Null)) }

pub fn steel_plan_remove(mut installed_json: SVal, mut catalogue_json: SVal, mut target_id: SVal, mut force: SVal) -> SVal {
    let mut installed = __st_json_parse(&installed_json);
    let mut catalogue = __st_json_parse(&catalogue_json);
    return __st_json_stringify(&(plan_remove(installed.clone(), catalogue.clone(), target_id.clone(), force.clone())));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_steel_plan_remove(__a: Vec<SVal>) -> SVal { steel_plan_remove(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null), __a.get(2).cloned().unwrap_or(SVal::Null), __a.get(3).cloned().unwrap_or(SVal::Null)) }

pub fn steel_cli_dispatch(mut args_json: SVal, mut state_json: SVal, mut catalogue_json: SVal, mut timestamp: SVal) -> SVal {
    let mut args = __st_json_parse(&args_json);
    let mut state = (if ((&(__st_len(&(__st_trim(&state_json))))).as_num() > (&(SVal::Num(0.000000))).as_num()) { __st_json_parse(&state_json) } else { db_default_state() });
    let mut catalogue = (if ((&(__st_len(&(__st_trim(&catalogue_json))))).as_num() > (&(SVal::Num(0.000000))).as_num()) { __st_json_parse(&catalogue_json) } else { SVal::Map(Rc::new(RefCell::new(SMap::new()))) });
    return __st_json_stringify(&(cli_dispatch(args.clone(), state.clone(), catalogue.clone(), timestamp.clone())));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_steel_cli_dispatch(__a: Vec<SVal>) -> SVal { steel_cli_dispatch(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null), __a.get(2).cloned().unwrap_or(SVal::Null), __a.get(3).cloned().unwrap_or(SVal::Null)) }

pub fn steel_ui_reduce(mut ui_json: SVal, mut event_json: SVal, mut state_json: SVal, mut catalogue_json: SVal) -> SVal {
    let mut ui = (if ((&(__st_len(&(__st_trim(&ui_json))))).as_num() > (&(SVal::Num(0.000000))).as_num()) { __st_json_parse(&ui_json) } else { ui_initial_state() });
    let mut ev = __st_json_parse(&event_json);
    let mut st = (if ((&(__st_len(&(__st_trim(&state_json))))).as_num() > (&(SVal::Num(0.000000))).as_num()) { __st_json_parse(&state_json) } else { db_default_state() });
    let mut cat = (if ((&(__st_len(&(__st_trim(&catalogue_json))))).as_num() > (&(SVal::Num(0.000000))).as_num()) { __st_json_parse(&catalogue_json) } else { SVal::Map(Rc::new(RefCell::new(SMap::new()))) });
    return __st_json_stringify(&(ui_reduce(ui.clone(), ev.clone(), st.clone(), cat.clone())));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_steel_ui_reduce(__a: Vec<SVal>) -> SVal { steel_ui_reduce(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null), __a.get(2).cloned().unwrap_or(SVal::Null), __a.get(3).cloned().unwrap_or(SVal::Null)) }

pub fn steel_build_manifest_json(mut id: SVal, mut version: SVal, mut scope: SVal, mut install_path: SVal, mut sha256: SVal, mut signature: SVal, mut files_json: SVal) -> SVal {
    let mut files = __st_json_parse(&files_json);
    return __st_json_stringify(&(build_package_manifest(id.clone(), version.clone(), scope.clone(), install_path.clone(), sha256.clone(), signature.clone(), files.clone())));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_steel_build_manifest_json(__a: Vec<SVal>) -> SVal { steel_build_manifest_json(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null), __a.get(2).cloned().unwrap_or(SVal::Null), __a.get(3).cloned().unwrap_or(SVal::Null), __a.get(4).cloned().unwrap_or(SVal::Null), __a.get(5).cloned().unwrap_or(SVal::Null), __a.get(6).cloned().unwrap_or(SVal::Null)) }

pub fn steel_publish_entry_json(mut catalogue_json: SVal, mut id: SVal, mut entry_json: SVal) -> SVal {
    let mut cat = (if ((&(__st_len(&(__st_trim(&catalogue_json))))).as_num() > (&(SVal::Num(0.000000))).as_num()) { __st_json_parse(&catalogue_json) } else { SVal::Map(Rc::new(RefCell::new(SMap::new()))) });
    let mut entry = __st_json_parse(&entry_json);
    return __st_json_stringify(&(publish_catalogue_entry(cat.clone(), id.clone(), entry.clone())));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_steel_publish_entry_json(__a: Vec<SVal>) -> SVal { steel_publish_entry_json(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null), __a.get(2).cloned().unwrap_or(SVal::Null)) }

pub fn steel_validate_helper_request_json(mut req_json: SVal) -> SVal {
    let mut req = __st_json_parse(&req_json);
    return __st_json_stringify(&(validate_helper_request(req.clone())));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_steel_validate_helper_request_json(__a: Vec<SVal>) -> SVal { steel_validate_helper_request_json(__a.get(0).cloned().unwrap_or(SVal::Null)) }

pub fn steel_helper_run_json(mut req_json: SVal) -> SVal {
    return __st_json_stringify(&(helper_execute_request(req_json.clone())));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_steel_helper_run_json(__a: Vec<SVal>) -> SVal { steel_helper_run_json(__a.get(0).cloned().unwrap_or(SVal::Null)) }

pub fn steel_runner_cli_json() -> SVal {
    return __st_json_stringify(&(runner_cli_main()));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_steel_runner_cli_json(__a: Vec<SVal>) -> SVal { steel_runner_cli_json() }

pub fn steel_runner_publish_json(mut cat_path: SVal, mut id: SVal, mut entry_path: SVal) -> SVal {
    return __st_json_stringify(&(runner_publish_catalogue(cat_path.clone(), id.clone(), entry_path.clone())));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_steel_runner_publish_json(__a: Vec<SVal>) -> SVal { steel_runner_publish_json(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null), __a.get(2).cloned().unwrap_or(SVal::Null)) }

pub fn steel_plan_snapshot_json(mut installed_json: SVal, mut catalogue_json: SVal, mut snapshot_json: SVal) -> SVal {
    let mut installed = __st_json_parse(&installed_json);
    let mut catalogue = __st_json_parse(&catalogue_json);
    let mut snapshot = __st_json_parse(&snapshot_json);
    return __st_json_stringify(&(plan_system_snapshot_transition(installed.clone(), catalogue.clone(), snapshot.clone())));
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_steel_plan_snapshot_json(__a: Vec<SVal>) -> SVal { steel_plan_snapshot_json(__a.get(0).cloned().unwrap_or(SVal::Null), __a.get(1).cloned().unwrap_or(SVal::Null), __a.get(2).cloned().unwrap_or(SVal::Null)) }

pub fn steel_run_self_tests() -> SVal {
    return run_all_steel_self_tests();
    SVal::Null
}
#[allow(dead_code)] fn __steel_fn_steel_run_self_tests(__a: Vec<SVal>) -> SVal { steel_run_self_tests() }

fn main() {
}
