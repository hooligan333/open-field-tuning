#!/usr/bin/env python3
"""Tunings for the pack `tuning`: Scala scales to .oft files, and back.

  oft.py make OUT.oft [SCALE MAP] [--layer SCALE:MAP ...] [--ref-key K] [--ref-hz H]
  oft.py show FILE.oft
  oft.py check FILE.oft ...
  oft.py header OUT.h GROUP/NAME=FILE.oft|GROUP/NAME=SCALE:MAP ...

MAP: linear, white7, white5, black5, chromatic12, or a .kbm file. Layers: later ones fill keys earlier ones left; a
key no layer maps plays the nearest mapped key below it (above, at the very bottom). Degree 0 = 1/1 sits on C4 =
key 60 (C#4 = key 61 for black5) at its equal-tempered pitch; --ref-key/--ref-hz move the whole tuning so that key K
sounds H Hz (K alone: its equal-tempered pitch; H alone: K = 60).

header: the built-ins in the given order, each in its group (small letters, digits, spaces; a group has no hyphen).

.oft (648 bytes): "OFT1", src[128] bytes, ratio[128] u32 LE (Q2.30), CRC-32 of bytes 0..643 LE. The unit computes
entry[n] = floor(stock[src[n]] * ratio[n] / 2^30) from its own table; here stock is the 12-tone equal formula.
Then every entry times TC_CENTS[d + 50] (the fourth knob, d cents); make and check refuse a tuning that leaves
1..0x3fffffff at any root 0..11 at 0, -50 or +50 cents.
"""
import math, os, re, sys, zlib

NOTES, BLOCK, FILE, ONE, MOST = 128, 640, 648, 0x40000000, 0x3fffffff
MAGIC = b"OFT1"
STOCK_SUM = 0x492003fc          # CRC-32 of the unit's stock table; stock() below must give it
NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]


class Refused(Exception):
    pass


# ---- the unit's arithmetic (reference) ----

def stock():
    """12-tone equal table as the unit holds it, by formula: round(440*2^((n-69)/12)*2^32/88200)."""
    return [round(440 * 2 ** ((n - 69) / 12) * 2 ** 32 / 88200) for n in range(NOTES)]


def table_sum(words):
    return zlib.crc32(b"".join(w.to_bytes(4, "little") for w in words))


def mul_q30(a, r):
    v = (a * r) >> 30
    return v if v < 2 ** 32 else None


DETUNE = 50                     # the fourth knob: -50..+50 cents, one cent a step


def cents_ratio(d):
    """Q2.30 ratio of d cents: round(2^(d/1200) * 2^30); d = 0 is exactly ONE."""
    return ONE if d == 0 else round(2 ** (d / 1200) * ONE)


CENTS = [cents_ratio(d) for d in range(-DETUNE, DETUNE + 1)]   # TC_CENTS[d + 50]


def detune(entries, d):
    """entries times d cents, floor, as tc_detune does; None when any result leaves 1..MOST."""
    r = CENTS[d + DETUNE]
    out = [(e * r) >> 30 for e in entries]
    return out if all(1 <= e <= MOST for e in out) else None


def valid_everywhere(st, src, ratio):
    """None when the tuning builds at every root 0..11 and stays valid at -50 and +50 cents, else (root, d, key or None)."""
    for root in range(12):
        e, bad = build(st, src, ratio, root)
        if e is None:
            return root, 0, bad
        for d in (-DETUNE, DETUNE):
            if detune(e, d) is None:
                return root, d, None
    return None


def build(st, src, ratio, root):
    """entry[128] for a root 0..11 by the pack's rule, or None (with the first bad key) when not valid."""
    out = []
    for n in range(NOTES):
        m = n - root
        if m < 0:
            out.append(st[n]); continue
        s = src[m] + root
        if src[m] > 127 or s > 127:
            if src[m] > 127:
                return None, n
            out.append(st[n]); continue
        e = mul_q30(st[s], ratio[m])
        if e is None or not 1 <= e <= MOST:
            return None, n
        out.append(e)
    return out, None


# ---- Scala files ----

def _lines(path):
    with open(path, "rb") as f:
        text = f.read().decode("latin-1")
    return [l.rstrip("\r") for l in text.split("\n")]


def parse_scl(path):
    """Cents of degrees 0..N (0 implied); the last is the period."""
    body = [l for l in _lines(path) if not l.startswith("!")]
    if len(body) < 2:
        raise Refused(f"{path}: no description or note count")
    try:
        count = int(body[1].strip())
    except ValueError:
        raise Refused(f"{path}: note count line is not a number: {body[1]!r}")
    if count < 1:
        raise Refused(f"{path}: needs at least one pitch")
    if len(body) - 2 < count:
        raise Refused(f"{path}: {count} pitches announced, {len(body) - 2} lines found")
    cents = [0.0]
    for i, line in enumerate(body[2:2 + count], 1):
        tok = line.split()
        if not tok:
            raise Refused(f"{path}: pitch {i} is an empty line")
        cents.append(pitch_cents(tok[0], f"{path}: pitch {i}"))
    if cents[-1] <= 0:
        raise Refused(f"{path}: period (last pitch) must be above 1/1, is {cents[-1]:.3f} cents")
    return cents


def pitch_cents(t, where):
    if "." in t:
        try:
            return float(t)
        except ValueError:
            raise Refused(f"{where}: not a cents value: {t!r}")
    m = re.fullmatch(r"(-?\d+)(?:/(-?\d+))?", t)
    if not m:
        raise Refused(f"{where}: not a ratio: {t!r}")
    a, b = int(m.group(1)), int(m.group(2) or 1)
    if a <= 0 or b <= 0:
        raise Refused(f"{where}: ratio must be positive: {t!r}")
    return 1200 * math.log2(a / b)


def parse_kbm(path):
    vals = []
    for l in _lines(path):
        if l.startswith("!") or not l.strip():
            continue
        vals.append(l.split()[0])
    if len(vals) < 7:
        raise Refused(f"{path}: needs 7 header fields, found {len(vals)}")
    try:
        size, first, last, middle, ref = (int(v) for v in vals[:5])
        hz = float(vals[5]); octave = int(vals[6])
    except ValueError as e:
        raise Refused(f"{path}: bad header field ({e})")
    if size < 0 or hz <= 0:
        raise Refused(f"{path}: bad map size or reference frequency")
    degs = []
    for v in vals[7:7 + size]:
        if v.lower() == "x":
            degs.append(None)
        else:
            try:
                degs.append(int(v))
            except ValueError:
                raise Refused(f"{path}: mapping entry is neither a degree nor x: {v!r}")
    degs += [None] * (size - len(degs))
    return dict(size=size, first=first, last=last, middle=middle, ref=ref, hz=hz, octave=octave, map=degs)


X = None
NAMED = {   # map, keys a period, degree-0 key
    "chromatic12": ([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11], 12, 60),
    "white7": ([0, X, 1, X, 2, 3, X, 4, X, 5, X, 6], 7, 60),
    "white5": ([0, X, 1, X, 2, X, X, 3, X, 4, X, X], 5, 60),
    "black5": ([0, X, 1, X, X, 2, X, 3, X, 4, X, X], 5, 61),
}


def named_kbm(name, ndeg):
    if name == "linear":
        return dict(size=0, first=0, last=127, middle=60, ref=60, hz=None, octave=ndeg, map=[])
    if name not in NAMED:
        raise Refused(f"unknown map {name!r} (linear, white7, white5, black5, chromatic12 or a .kbm file)")
    m, need, mid = NAMED[name]
    if ndeg != need:
        raise Refused(f"map {name} wants a {need}-note scale, this one has {ndeg}")
    return dict(size=12, first=0, last=127, middle=mid, ref=mid, hz=None, octave=need, map=m)


def layer_pitches(scale_path, map_spec):
    """{key: absolute pitch in cents, 100 per equal semitone, key n's equal pitch = 100n}."""
    cents = parse_scl(scale_path)
    n = len(cents) - 1
    k = parse_kbm(map_spec) if map_spec.endswith(".kbm") else named_kbm(map_spec, n)

    def deg_of(key):
        d = key - k["middle"]
        if k["size"] == 0:
            return d
        i, o = d % k["size"], d // k["size"]
        return None if k["map"][i] is None else k["map"][i] + o * k["octave"]

    def c(D):
        q, r = divmod(D, n)
        return q * cents[n] + cents[r]

    ref_deg = deg_of(k["ref"])
    if ref_deg is None:
        raise Refused(f"{map_spec}: reference key {k['ref']} is not mapped")
    base = 100 * k["ref"] if k["hz"] is None else 6900 + 1200 * math.log2(k["hz"] / 440)
    out = {}
    for key in range(max(0, k["first"]), min(127, k["last"]) + 1):
        D = deg_of(key)
        if D is not None:
            out[key] = base + c(D) - c(ref_deg)
    return out


def pitches(layers, ref_key=None, ref_hz=None):
    p = {}
    for scl, mp in layers:
        for key, v in layer_pitches(scl, mp).items():
            p.setdefault(key, v)
    if not p:
        raise Refused("no layer maps any key")
    lo = min(p)
    full, last = [], p[lo]
    for key in range(NOTES):
        last = p.get(key, last)
        full.append(last)
    if ref_key is not None or ref_hz is not None:
        rk = 60 if ref_key is None else ref_key
        target = 100 * rk if ref_hz is None else 6900 + 1200 * math.log2(ref_hz / 440)
        shift = target - full[rk]
        full = [v + shift for v in full]
    return full


def encode(full):
    """Pitches (cents) to src and ratio; refuses keys the unit could not play, naming them."""
    src, ratio = [], []
    for n, P in enumerate(full):
        lo = math.floor(P / 100)
        cand = [s for s in (lo, lo + 1)]
        dev = [abs(P - 100 * s) for s in cand]
        if abs(dev[0] - dev[1]) < 1e-9:
            s = n if n in cand else cand[0]
        else:
            s = cand[dev.index(min(dev))]
        s = min(127, max(0, s))
        r = round(ONE * 2 ** ((P - 100 * s) / 1200))
        if not 1 <= r < 2 ** 32:
            raise Refused(f"key {n} ({note_name(n)}): {hz(P):.6g} Hz is out of reach")
        src.append(s); ratio.append(r)
    bad = valid_everywhere(stock(), src, ratio)
    if bad:
        root, d, key = bad
        if key is None:
            raise Refused(f"an entry leaves 1..0x3fffffff with root {NAMES[root]} at {d:+d} cents")
        m = key - root
        raise Refused(f"key {m} ({note_name(m)}, {hz(full[m]):.4f} Hz): entry leaves 1..0x3fffffff"
                      f"{'' if root == 0 else f' with root {NAMES[root]}'}")
    return src, ratio


def pack_file(src, ratio):
    body = MAGIC + bytes(src) + b"".join(r.to_bytes(4, "little") for r in ratio)
    return body + zlib.crc32(body).to_bytes(4, "little")


def read_file(data):
    """(src, ratio) or raises Refused with what is wrong."""
    if len(data) != FILE:
        raise Refused(f"size {len(data)}, want {FILE}")
    if data[:4] != MAGIC:
        raise Refused("magic is not OFT1")
    if zlib.crc32(data[:644]) != int.from_bytes(data[644:], "little"):
        raise Refused("CRC-32 does not match")
    return list(data[4:132]), [int.from_bytes(data[132 + 4 * i:136 + 4 * i], "little") for i in range(NOTES)]


def check(data):
    src, ratio = read_file(data)
    for n, s in enumerate(src):
        if s > 127:
            raise Refused(f"key {n}: source note {s} above 127")
    bad = valid_everywhere(stock(), src, ratio)
    if bad:
        root, d, key = bad
        where = f"key {key}" if key is not None else f"at {d:+d} cents, an entry"
        raise Refused(f"{where}: entry leaves 1..0x3fffffff with root {NAMES[root]}")
    return src, ratio


def note_name(n):
    return f"{NAMES[n % 12]}{n // 12 - 1}"


def hz(P):
    return 440 * 2 ** ((P - 6900) / 1200)


def file_cents(src, ratio):
    """Pitch of each key (cents, 100n = equal), exactly as the file says."""
    return [100 * s + 1200 * math.log2(r / ONE) for s, r in zip(src, ratio)]


def show(data, out=sys.stdout):
    src, ratio = check(data)
    print("key  note      Hz          cents vs equal  src", file=out)
    for n, P in enumerate(file_cents(src, ratio)):
        print(f"{n:3d}  {note_name(n):5s} {hz(P):11.4f}  {P - 100 * n:+14.3f}  {src[n]:3d}", file=out)


# ---- header ----

HNAME = re.compile(r"[a-z0-9 ]{1,23}")


def header(entries):
    """entries: (group, name, 648-byte file) in the pack's order."""
    lines = ["// Generated by tuning/tools/oft.py header; do not edit by hand. Built-in tunings: 640-byte tuning blocks",
             "// (src[128], ratio[128] u32 LE Q2.30) for tonic C. No firmware bytes.",
             "#ifndef TUNING_BUILTINS_H", "#define TUNING_BUILTINS_H", "",
             "struct TcBuiltin { const char* group; const char* name; const unsigned char* block; };", ""]
    for i, (group, name, data) in enumerate(entries):
        block = data[4:644]
        lines.append(f"static const unsigned char TC_BLOCK_{i}[{BLOCK}] = {{  // {group} / {name}")
        for j in range(0, BLOCK, 20):
            lines.append("    " + ",".join(f"0x{b:02x}" for b in block[j:j + 20]) + ",")
        lines.append("};")
    lines += ["", "static const TcBuiltin TC_BUILTINS[] = {"]
    lines += [f'    {{ "{group}", "{name}", TC_BLOCK_{i} }},' for i, (group, name, _) in enumerate(entries)]
    lines += ["};", f"enum {{ TC_BUILTIN_COUNT = {len(entries)} }};", ""]
    lines += [f"// the fourth knob: TC_CENTS[d + {DETUNE}] = round(2^(d/1200) * 2^30), d = -{DETUNE}..+{DETUNE} cents",
              f"enum {{ TC_DETUNE_MOST = {DETUNE} }};", f"static const unsigned int TC_CENTS[{2 * DETUNE + 1}] = {{"]
    for j in range(0, len(CENTS), 6):
        lines.append("    " + ", ".join(f"0x{r:08x}" for r in CENTS[j:j + 6]) + ",")
    lines += ["};", "", "#endif", ""]
    return "\n".join(lines)


# ---- command line ----

def split_layer(spec):
    if ":" not in spec:
        raise Refused(f"layer {spec!r}: want SCALE:MAP")
    s, m = spec.rsplit(":", 1)
    return s, m


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__); return 0
    cmd, rest = argv[0], argv[1:]
    try:
        if cmd == "make":
            layers, pos, ref_key, ref_hz = [], [], None, None
            it = iter(rest)
            for a in it:
                if a == "--layer": layers.append(split_layer(next(it)))
                elif a == "--ref-key": ref_key = int(next(it))
                elif a == "--ref-hz": ref_hz = float(next(it))
                else: pos.append(a)
            if ref_key is not None and not 0 <= ref_key <= 127:
                raise Refused("--ref-key must be 0..127")
            if ref_hz is not None and not ref_hz > 0:
                raise Refused("--ref-hz must be above 0")
            if len(pos) == 3:
                layers.insert(0, (pos[1], pos[2]))
            elif len(pos) != 1:
                raise Refused("make OUT.oft [SCALE MAP] [--layer SCALE:MAP ...]")
            if not layers:
                raise Refused("no scale given")
            out = pos[0]
            base = os.path.basename(out)
            if not re.fullmatch(r"[a-z0-9 _-]{1,39}\.oft", base):
                print(f"note: name {base!r} is not 1-39 small letters, digits, space, - or _ plus .oft", file=sys.stderr)
            src, ratio = encode(pitches(layers, ref_key, ref_hz))
            with open(out, "wb") as f:
                f.write(pack_file(src, ratio))
            print(f"{out}: written")
        elif cmd == "show":
            if len(rest) != 1: raise Refused("show FILE.oft")
            show(open(rest[0], "rb").read())
        elif cmd == "check":
            if not rest: raise Refused("check FILE.oft ...")
            bad = 0
            for p in rest:
                try:
                    check(open(p, "rb").read()); print(f"{p}: ok")
                except Refused as e:
                    print(f"{p}: REFUSED: {e}"); bad += 1
            return 1 if bad else 0
        elif cmd == "header":
            if len(rest) < 2: raise Refused("header OUT.h GROUP/NAME=FILE ...")
            entries = []
            for e in rest[1:]:
                full, _, what = e.partition("=")
                group, slash, name = full.partition("/")
                if not slash or not HNAME.fullmatch(group):
                    raise Refused(f"built-in {full!r}: want GROUP/NAME, the group small letters, digits, spaces, 1..23")
                if not HNAME.fullmatch(name):
                    raise Refused(f"built-in name {name!r}: small letters, digits, spaces, 1..23")
                if group == "user":
                    raise Refused("the group 'user' is the pack's own, for files")
                if any(name == n for _, n, _ in entries):
                    raise Refused(f"built-in name {name!r} given twice (the settings file keeps the name alone)")
                if what.endswith(".oft"):
                    data = open(what, "rb").read()
                    check(data)
                else:
                    data = pack_file(*encode(pitches([split_layer(what)])))
                entries.append((group, name, data))
            with open(rest[0], "w") as f:
                f.write(header(entries))
            print(f"{rest[0]}: {len(entries)} built-ins")
        else:
            raise Refused(f"unknown command {cmd!r}")
    except Refused as e:
        print(f"oft.py {cmd}: REFUSED: {e}", file=sys.stderr)
        return 2
    except (OSError, StopIteration, ValueError) as e:
        print(f"oft.py {cmd}: error: {e or 'missing value'}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
