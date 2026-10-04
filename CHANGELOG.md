# FaceForgeMetaHuman

Every released version of FaceForgeMetaHuman, newest first. A release publishes **one** section of this
file — the one whose heading matches its tag — as its release notes; for an `open` plugin those
notes are posted to Discord `#releases` automatically. Write for someone who installs the plugin,
not for the commit log.

Headings are `## <x.y.z> — <date>`. Use `Added` / `Changed` / `Fixed` / `Compatibility` /
`Known issues`, only the ones that apply.

## 0.2.2 — 2026-10-04

### Changed
- Copyright and licence notices now name Bojan Andrejek / MetaWorx LLC. It is still Apache 2.0, and nothing about how you may use it changed.

## 0.2.1 — 2026-09-08
- Packaging fix: the release now carries everything the register allows. `BuildPlugin`'s filter excludes `Config/` and every `public_extra` path, so earlier zips shipped without them.

## 0.2.0 — 2026-09-07
- Apache-2.0 relicensing; every plugin descriptor made to agree with its release tag
- The video solve shipped: a webcam take through MetaHuman Animator, into a layer
- PerformanceForge's toolset moved out; provider toolsets stay with their providers
- A provider now earns a toolset only by having something only it can do
- Hand correction shipped as a layer, so the solve underneath can be replaced
- Repointed to kovati.dev

## 0.1.0 — 2026-08-28
- Initial release: the engine's own solver, and the conversion it needs
