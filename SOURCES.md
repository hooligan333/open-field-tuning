# tuning: where each tuning's figures come from

| Tuning | Source |
| --- | --- |
| `equal / 12 equal`, `equal / 24 equal` | By definition: 100 and 50 cents a key |
| `maqam / rast`, `maqam / bayati` | Written from the 24-tone theory of the maqam (half-flats at 50 cents); `scales/rast.scl`, `scales/bayati.scl` |
| `pure / just` | 5-limit ratios on C, by definition; `scales/just.scl` |
| `pure / pythagorean` | Pure 3/2 fifths from Eb to G#, by definition; `scales/pythagorean.scl` |
| `gamelan / slendro`, `gamelan / pelog`, `gamelan / gamelan`; the files `slendro mesem`, `pelog mesem`, `mesem` and their `black` variants | Gamelan Kyahi Kanyutmesem, Mangkunegaran, Surakarta: measured, see below |
| The files `slendro madu`, `pelog siratmadu`, `jogja` and `slendro madu black` | Gamelan Kyahi Madukentir (slendro) and Kyahi Siratmadu (pelog), kraton Yogyakarta: measured, see below |

## The measured gamelan figures

Measured by Wasisto Surjodiningrat, P. J. Sudarjana and Adhi Susanto, *Tone Measurements of Outstanding Javanese
Gamelans in Jogjakarta and Surakarta*, Gadjah Mada University Press, Yogyakarta, 1972 (second edition 1993): tables
8 and 9, the tunings of the saron demung or gender barung.

Taken, in cents, from the Database of Musical Scales (DaMuSc), entries M0344, M0350, M0368 and M0390: John M.
McBride, Sam Passmore and Tsvi Tlusty, "Convergent evolution in a large cross-cultural database of musical scales",
PLoS ONE 18(12): e0284851, 2023, https://doi.org/10.1371/journal.pone.0284851; data
https://doi.org/10.5281/zenodo.7250281, licensed **CC BY 4.0** (https://creativecommons.org/licenses/by/4.0/). The
cents are the database's conversion of the book's frequencies; here each scale starts from its first tone, with the
octave as measured. The figures in `scales/*.scl`, `tunings/*.oft` and `source/builtins.h` are that data in other
forms; the rest of the pack is under its own licence.

| Scale file | Steps in cents | Octave |
| --- | --- | --- |
| `scales/slendro-mesem.scl` | 251, 238, 241, 236, 253 | 1219 |
| `scales/pelog-mesem.scl` | 141, 140, 272, 139, 114, 172, 245 | 1223 |
| `scales/slendro-madu.scl` | 237, 229, 256, 242, 249 | 1213 |
| `scales/pelog-siratmadu.scl` | 134, 129, 291, 111, 135, 171, 235 | 1206 |

That Madukentir and Siratmadu are the two halves of one gamelan is from Roger Vetter's catalogue of the kraton
Yogyakarta's gamelans (vetter.sites.grinnell.edu/gamelan).

Not checked against the book itself: the cents are the database's conversion of the book's frequencies. A second
reading of the same book (the booklet of "Ten Gamelan Tunings for Debussy's Nuages", Yantra) gives Kanyutmesem's
pelog steps the same to the cent and its slendro steps within 4 cents.
