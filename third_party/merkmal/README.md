# merkmal, pinned

A byte-exact copy of the merkmal C core at the commit named in `PINNED`. It is
committed rather than fetched so that a source install, a CMake build and the
WebAssembly build all compile the same phonology, with no second checkout and
no network.

The pin is not incidental. cognator's clustering thresholds are calibrated
against a particular segment-distance scale, and merkmal's descriptive feature
inventory — and with it that scale — changes between releases. Building against
a different merkmal produces different clusters from the ones every benchmark
number in this repository was measured on. `scripts/merkmal-provenance.sh
--check` fails if the tree here stops matching what `PINNED` records.

To move the pin, see `docs/merkmal_pin.md`: it is a recalibration, not an
upgrade.

Upstream is MIT-licensed; `LICENSE` is the upstream file, copied unmodified.
