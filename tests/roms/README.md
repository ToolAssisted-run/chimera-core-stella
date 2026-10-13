# The test roms in this folder

Two homebrew games the tests run, so that the tests can run on a public CI
runner. Each is here as its author published it, for free download. Neither
is under this repository's licence, and for neither was a licence found that
says the file may be passed on (looked 2026-10-13): if you are the author of
one and want it removed, say so in an issue and it goes.

| file | what it is | by | where it was published |
| --- | --- | --- | --- |
| `hellway.a26` | Hellway, a 4K racing game | opbokel | <https://github.com/opbokel/hellway> (no licence file) |
| `berryfun.bin` | Berry Fun! 1.0 (2023), an 8K game | Littau | the AtariAge forum, where the author posted the rom; AtariAge also sells it as a cartridge |

Commercial cartridges are never put here: they stay in `tests/roms-local/`,
which git ignores, and their tests are skipped where they are absent.
