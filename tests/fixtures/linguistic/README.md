# C linguistic smoke fixtures

These are compact, pinned fixtures for the C rewrite's milestone checks.
They are not replacements for full CoreCog benchmarking.

- `walworth_polynesian_smoke.tsv` keeps a small Polynesian-style subset with
  regular near-identical cognate forms across Hawaiian, Samoan, Tongan, and
  Maori labels.
- `latin_romance_triplets.tsv` checks Latin-Romance reflexes such as
  `nokte`/`notte`/`note`, `okto`/`otto`/`oto`, and a non-cognate fire split.
- `latin_romance_extended.tsv` widens that check to four lects and six concepts,
  including stronger Romance reductions and two deliberate Latin/non-Latin
  lexical splits.
- `partial_cognacy_compounds.tsv` is synthetic, with `+` morpheme boundaries:
  three lects (Ala, Bera, Cumo; Bera voices stops, Cumo raises mid vowels),
  six compounds whose first morpheme is cognate in all three and whose second
  is replaced in Cumo, six roots carrying the same two-segment suffix `n a`
  in every lect, and two words without boundaries. `test_pairs` and
  `cli_pairs` use it for the pairwise export (`docs/pairs.md`): shared
  morphemes strong, replaced ones weak, the shared suffix penalised against an
  identical root at the same distance.

The CTest `linguistic_smoke` test runs the C CLI `detect` command on both
fixtures, then scores the sidecars with the C CLI `eval` command. These checks
guard the end-to-end path and catch obvious linguistic regressions before the
larger Walworth CoreCog benchmark is available.
