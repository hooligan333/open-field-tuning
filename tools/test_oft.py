#!/usr/bin/env python3
"""Tests for oft.py.  test_oft.py [RECORD]  (RECORD: fw-records-1.7.3/235_848c50b0.bin; when given, the formula
table is compared with it; no value of it is printed or kept)."""
import io, math, os, sys, tempfile, unittest, zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oft  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
SCALES = os.path.join(HERE, "..", "scales")
RECORD = None


def unit_entries(src, ratio, root):
    """Independent restatement of the unit's rule, by plain integers."""
    st = oft.stock()
    out = []
    for n in range(128):
        m = n - root
        if m < 0 or src[m] + root > 127:
            out.append(st[n])
        else:
            out.append(st[src[m] + root] * ratio[m] // 2 ** 30)
    return out


class T(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp()

    def w(self, name, text):
        p = os.path.join(self.d, name)
        with open(p, "w") as f:
            f.write(text)
        return p

    def make(self, layers, **kw):
        src, ratio = oft.encode(oft.pitches(layers, **kw))
        return src, ratio, oft.pack_file(src, ratio)

    def et12(self):
        return self.w("et.scl", "! et.scl\n!\n12 equal\n 12\n!\n" + "".join(f" {100 * k}.0\n" for k in range(1, 12)) + " 2/1\n")

    # -- the arithmetic
    def test_stock_formula_sum(self):
        self.assertEqual(oft.table_sum(oft.stock()), oft.STOCK_SUM)

    def test_stock_formula_matches_record(self):
        if not RECORD:
            self.skipTest("no record file given")
        data = open(RECORD, "rb").read()[0x1c0:0x1c0 + 512]
        self.assertEqual(zlib.crc32(data), oft.STOCK_SUM)
        self.assertTrue(all(int.from_bytes(data[4 * n:4 * n + 4], "little") == w for n, w in enumerate(oft.stock())),
                        "formula table differs from the record")

    def test_reference_matches_restatement(self):
        for scl, mp in (("rast", "chromatic12"), ("24equal", "linear"), ("slendro-madu", "black5")):
            src, ratio, _ = self.make([(os.path.join(SCALES, scl + ".scl"), mp)])
            for root in range(12):
                e, _ = oft.build(oft.stock(), src, ratio, root)
                self.assertEqual(e, unit_entries(src, ratio, root), (scl, root))

    def test_mul_q30(self):
        self.assertEqual(oft.mul_q30(0xffffffff, oft.ONE), 0xffffffff)
        self.assertIsNone(oft.mul_q30(0xffffffff, oft.ONE + 1))
        self.assertEqual(oft.mul_q30(12345, 0x20000000), 6172)

    # -- 12 equal and quarter tones
    def test_12tet_identity(self):
        src, ratio, data = self.make([(self.et12(), "chromatic12")])
        self.assertEqual(src, list(range(128)))
        self.assertTrue(all(r == 0x40000000 for r in ratio))
        for root in range(12):
            self.assertEqual(oft.build(oft.stock(), src, ratio, root)[0], oft.stock())
        lin = self.make([(self.et12(), "linear")])[2]
        self.assertEqual(lin, data)

    def test_quarter_flat_e_b(self):
        p = self.w("q.scl", "! q\nE and B quarter-flat\n12\n100.\n200.\n300.\n350.\n500.\n600.\n700.\n800.\n900.\n1000.\n1050.\n2/1\n")
        src, ratio, data = self.make([(p, "chromatic12")])
        cents = oft.file_cents(src, ratio)
        for n in range(128):
            want = -50.0 if n % 12 in (4, 11) else 0.0
            self.assertAlmostEqual(cents[n] - 100 * n, want, delta=0.0005, msg=n)
        out = io.StringIO(); oft.show(data, out)
        rows = out.getvalue().splitlines()[1:]
        self.assertEqual(sum("-50.000" in r for r in rows), len([n for n in range(128) if n % 12 in (4, 11)]))
        self.assertTrue(all(("-50.000" in r) == (int(r.split()[0]) % 12 in (4, 11)) for r in rows))

    # -- Scala parsing
    def test_scl_lines(self):
        p = self.w("a.scl", "! a.scl\n!\n\n 6\n!\n 100.0 cents\n\t408.\n 81/64 E\n 5\n -5.0\n 2/1 octave\n")
        c = oft.parse_scl(p)
        self.assertEqual(len(c), 7)
        self.assertAlmostEqual(c[1], 100.0); self.assertAlmostEqual(c[2], 408.0)
        self.assertAlmostEqual(c[3], 1200 * math.log2(81 / 64)); self.assertAlmostEqual(c[4], 1200 * math.log2(5))
        self.assertAlmostEqual(c[5], -5.0); self.assertAlmostEqual(c[6], 1200.0)

    def test_scl_refusals(self):
        for body in ("d\n2\n-3/2\n2/1\n", "d\n2\n3/0\n2/1\n", "d\n3\n100.\n2/1\n", "d\nx\n", "d\n1\nabc\n",
                     "d\n1\n-100.0\n", "d\n2\n100.\n\n"):
            with self.assertRaises(oft.Refused, msg=body):
                oft.parse_scl(self.w("r.scl", body))

    def test_kbm_x_and_size(self):
        s = self.w("s.scl", "s\n 3\n 400.\n 700.\n 1200.\n")
        k = self.w("m.kbm", "! map\n7\n0\n127\n60\n60\n261.6255653005986\n3\n! degrees\n0\nx\n1\nx\n2\nx\n! last entry left out\n")
        p = oft.layer_pitches(s, k)
        self.assertNotIn(61, p); self.assertNotIn(63, p); self.assertNotIn(65, p); self.assertNotIn(66, p)
        self.assertAlmostEqual(p[60], 6000); self.assertAlmostEqual(p[62], 6400); self.assertAlmostEqual(p[64], 6700)
        self.assertAlmostEqual(p[67], 7200)          # next pattern: degree 3 = one period up
        self.assertAlmostEqual(p[53], 4800)
        full = oft.pitches([(s, k)])
        self.assertAlmostEqual(full[61], full[60])   # unmapped: nearest mapped below
        k2 = self.w("r.kbm", "5\n10\n100\n60\n69\n440\n5\n0\n1\n2\n3\n4\n")
        p2 = oft.layer_pitches(self.et12(), k2)
        self.assertEqual(min(p2), 10); self.assertEqual(max(p2), 100)
        self.assertAlmostEqual(p2[69] - p2[60], 900)  # degree 9 of 12-equal at key 69 = 440 Hz
        self.assertAlmostEqual(p2[69], 6900)
        with self.assertRaises(oft.Refused):        # reference key unmapped
            oft.layer_pitches(s, self.w("u.kbm", "2\n0\n127\n60\n61\n440\n3\n0\nx\n"))

    def test_named_maps(self):
        pel = os.path.join(SCALES, "pelog-mesem.scl"); sle = os.path.join(SCALES, "slendro-mesem.scl")
        w7 = oft.layer_pitches(pel, "white7")
        self.assertEqual(sorted(k % 12 for k in w7 if 60 <= k < 72), [0, 2, 4, 5, 7, 9, 11])
        self.assertAlmostEqual(w7[62] - w7[60], 141.0)       # pelog 1 to 2 of Kanyutmesem (1972)
        b5 = oft.layer_pitches(sle, "black5")
        self.assertEqual(sorted(k % 12 for k in b5 if 60 <= k < 72), [1, 3, 6, 8, 10])
        self.assertAlmostEqual(b5[61], 6100); self.assertAlmostEqual(b5[63] - b5[61], 251.0)   # slendro 1 to 2
        w5 = oft.layer_pitches(sle, "white5")
        self.assertEqual(sorted(k % 12 for k in w5 if 60 <= k < 72), [0, 2, 4, 7, 9])
        with self.assertRaises(oft.Refused):
            oft.layer_pitches(pel, "white5")

    def test_layers(self):
        pel = os.path.join(SCALES, "pelog-mesem.scl"); sle = os.path.join(SCALES, "slendro-mesem.scl")
        full = oft.pitches([(pel, "white7"), (sle, "black5")])
        w7, b5 = oft.layer_pitches(pel, "white7"), oft.layer_pitches(sle, "black5")
        for n in range(128):
            self.assertAlmostEqual(full[n], w7[n] if n in w7 else b5[n])
        # earlier layer wins where both map; later fills the rest
        e12 = self.et12()
        both = oft.pitches([(pel, "white7"), (e12, "chromatic12")])
        self.assertAlmostEqual(both[62], w7[62]); self.assertAlmostEqual(both[61], 6100)
        only_b = oft.pitches([(sle, "black5")])
        self.assertAlmostEqual(only_b[0], only_b[1])   # bottom key unmapped: nearest above
        self.assertAlmostEqual(only_b[62], only_b[61])

    def test_ref(self):
        full = oft.pitches([(self.et12(), "chromatic12")], ref_key=69, ref_hz=432.0)
        self.assertAlmostEqual(oft.hz(full[69]), 432.0)
        full = oft.pitches([(self.et12(), "chromatic12")], ref_hz=256.0)
        self.assertAlmostEqual(oft.hz(full[60]), 256.0)

    def test_refusals(self):
        hi = self.w("hi.scl", "too wide\n1\n2400.0\n")      # 2 octaves a key: leaves range
        with self.assertRaises(oft.Refused) as c:
            oft.encode(oft.pitches([(hi, "linear")]))
        self.assertIn("key", str(c.exception))
        # key 127 raised 900 cents still fits; 1000 cents leaves 1..0x3fffffff: refused, naming the key
        up = oft.pitches([(self.et12(), "chromatic12")])
        up[127] += 900
        oft.encode(up)
        up[127] += 100
        with self.assertRaises(oft.Refused) as c:
            oft.encode(up)
        self.assertIn("key 127", str(c.exception))
        with self.assertRaises(oft.Refused):
            oft.named_kbm("nonsense", 12)

    def test_round_trip_show(self):
        for scl, mp in (("just", "chromatic12"), ("slendro-madu", "white5"), ("24equal", "linear"), ("pythagorean", "chromatic12")):
            layers = [(os.path.join(SCALES, scl + ".scl"), mp)]
            want = oft.pitches(layers)
            data = self.make(layers)[2]
            out = io.StringIO(); oft.show(data, out)
            for n, row in enumerate(out.getvalue().splitlines()[1:]):
                got = float(row.split()[3]) + 100 * n
                self.assertLess(abs(got - want[n]), 0.001, (scl, n))

    def test_scale_period_by_hand(self):
        # slendro-madu's period is 1213 cents, not 1200 (Madukentir as measured, 1972): hand-computed pitches, in the
        # converter and in the file
        layers = [(os.path.join(SCALES, "slendro-madu.scl"), "white5")]
        p = oft.pitches(layers)
        cents = oft.file_cents(*self.make(layers)[:2])
        for key, want in ((60, 6000), (62, 6237), (72, 7213), (84, 8426), (48, 4787)):
            self.assertAlmostEqual(p[key], want, delta=1e-6, msg=key)
            self.assertLess(abs(cents[key] - want), 0.01, key)

    def test_check_flipped_bit(self):
        data = self.make([(os.path.join(SCALES, "rast.scl"), "chromatic12")])[2]
        oft.check(data)
        for pos in (0, 5, 300, 646):
            bad = bytearray(data); bad[pos] ^= 0x10
            with self.assertRaises(oft.Refused):
                oft.check(bytes(bad))
        with self.assertRaises(oft.Refused):
            oft.check(data[:-1])
        src = list(data[4:132]); src[3] = 200
        body = oft.MAGIC + bytes(src) + data[132:644]
        with self.assertRaises(oft.Refused):
            oft.check(body + zlib.crc32(body).to_bytes(4, "little"))

    # -- the third knob: detune
    def test_cents_table(self):
        from decimal import Decimal, getcontext
        getcontext().prec = 60
        self.assertEqual(len(oft.CENTS), 101)
        self.assertEqual(oft.CENTS[50], 0x40000000)
        for i, r in enumerate(oft.CENTS):
            exact = (Decimal(2) ** (Decimal(i - 50) / Decimal(1200))) * Decimal(2 ** 30)
            self.assertEqual(r, int(exact.to_integral_value(rounding="ROUND_HALF_EVEN")), i - 50)
            self.assertLess(abs(Decimal(r) - exact), Decimal("0.5"))
        self.assertTrue(all(a < b for a, b in zip(oft.CENTS, oft.CENTS[1:])))

    def test_detune_reference(self):
        st = oft.stock()
        self.assertEqual(oft.detune(st, 0), st)
        src, ratio, _ = self.make([(os.path.join(SCALES, "rast.scl"), "chromatic12")])
        for root in (0, 5, 11):
            e = oft.build(st, src, ratio, root)[0]
            for d in (-50, -17, 0, 3, 50):
                want = [x * round(2 ** (d / 1200) * 2 ** 30) // 2 ** 30 for x in unit_entries(src, ratio, root)]
                self.assertEqual(oft.detune(e, d), want, (root, d))
        self.assertIsNone(oft.detune([1] * 128, -1))
        self.assertIsNone(oft.detune([0x3f000000] * 128, 50))

    def _file(self, src, ratio):
        return oft.pack_file(src, ratio)

    def test_check_refuses_detune_extremes(self):
        st = oft.stock()
        src = list(range(128)); ratio = [oft.ONE] * 128
        oft.check(self._file(src, ratio))
        hi = list(ratio); hi[127] = (0x3f000000 << 30) // st[127]          # valid at 0, too high at +50
        self.assertTrue(1 <= (st[127] * hi[127]) >> 30 <= oft.MOST)
        with self.assertRaisesRegex(oft.Refused, r"\+50 cents"):
            oft.check(self._file(src, hi))
        lo = list(ratio); lo[0] = ((1 << 30) + st[0] - 1) // st[0]          # entry exactly 1 at 0: 0 at -50
        self.assertEqual((st[0] * lo[0]) >> 30, 1)
        with self.assertRaisesRegex(oft.Refused, r"-50 cents"):
            oft.check(self._file(src, lo))

    def test_make_refuses_detune_extremes(self):
        # 12 equal raised 950 cents: key 127 is in range at 0 cents (about 1.057e9) and out at +50; 900 cents passes
        hz127 = 440 * 2 ** ((127 - 69) / 12)
        self.make([(self.et12(), "chromatic12")], ref_key=127, ref_hz=hz127 * 2 ** (900 / 1200))
        with self.assertRaisesRegex(oft.Refused, r"\+50 cents"):
            self.make([(self.et12(), "chromatic12")], ref_key=127, ref_hz=hz127 * 2 ** (950 / 1200))

    def test_header_cents(self):
        out = os.path.join(self.d, "b.h")
        rast = os.path.join(self.d, "r.oft")
        with open(rast, "wb") as f:
            f.write(self.make([(os.path.join(SCALES, "rast.scl"), "chromatic12")])[2])
        self.assertEqual(oft.main(["header", out, "maqam/rast=" + rast]), 0)
        with open(out) as f:
            text = f.read()
        body = text.split("TC_CENTS[101] = {")[1].split("};")[0]
        vals = [int(v, 16) for v in body.replace(",", " ").split()]
        self.assertEqual(vals, oft.CENTS)
        self.assertIn("TC_DETUNE_MOST = 50", text)

    def test_header_groups(self):
        out = os.path.join(self.d, "g.h")
        rast = os.path.join(self.d, "r.oft")
        with open(rast, "wb") as f:
            f.write(self.make([(os.path.join(SCALES, "rast.scl"), "chromatic12")])[2])
        self.assertEqual(oft.main(["header", out, "maqam/rast=" + rast, "pure/24 equal=" + rast]), 0)
        with open(out) as f:
            text = f.read()
        self.assertIn("struct TcBuiltin { const char* group; const char* name; const unsigned char* block; };", text)
        self.assertIn('{ "maqam", "rast", TC_BLOCK_0 },', text)
        self.assertIn('{ "pure", "24 equal", TC_BLOCK_1 },', text)
        self.assertIn("TC_BUILTIN_COUNT = 2", text)
        for bad in (["rast=" + rast], ["/rast=" + rast], ["maqam/=" + rast], ["ma-qam/rast=" + rast], ["Maqam/rast=" + rast],
                    ["user/rast=" + rast], ["maqam/rast=" + rast, "pure/rast=" + rast]):
            self.assertEqual(oft.main(["header", out] + bad), 2, bad)

    def test_tc_values(self):
        self.assertEqual((oft.NOTES, oft.BLOCK, oft.FILE, oft.ONE, oft.MOST), (128, 640, 648, 0x40000000, 0x3fffffff))


if __name__ == "__main__":
    if len(sys.argv) > 1:
        RECORD = sys.argv.pop(1)
    unittest.main(verbosity=1)
