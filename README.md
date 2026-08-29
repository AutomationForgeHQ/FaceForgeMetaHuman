# FaceForgeMetaHuman

Registers **UE 5.8's own audio-driven animation solver** with FaceForge. One provider, one
registration, and nothing else — remove this plugin and FaceForge still loads with one fewer solver.

## What it wraps

`FSpeech2Face`, from the engine's MetaHuman Animator plugin. A `USoundWave` in, 81 face-board control
curves out, plus an optional head-pose track.

- **Free, local, offline.** Two ONNX models ship with the engine — a Whisper-derived audio encoder
  (~91 MB) and an animation decoder (~222 MB) — and run through NNE on the CPU. No account, no key, no
  network call, no per-use cost.
- **Deterministic.** The same audio and settings give the same curves, which makes the clip hash a real
  cache key rather than an approximation. A genuine difference from hosted speech models.
- 12 moods plus auto-detect, blink generation, and a mouth-only mask.

**No MetaHuman asset is required anywhere.** The API takes no identity, no DNA and no mesh — it is a
pure function from audio to named floats. The only MetaHuman-specific thing about it is the vocabulary
those names are in, and translating that is FaceForge's job, not this plugin's.

## Measured

Solving 1.76 s of speech took **0.9 s** on an i7-10700, after a one-off ~2 s model load. Throughput is
re-measured on every solve and fed back into the provider's caps, so estimates are based on this
machine rather than on a number somebody typed. It reports zero — meaning "unmeasured" — until the
first solve, deliberately, because an invented estimate reads exactly like a measured one.

## Three things worth knowing

**Creation is game-thread-only; solving is not.** The engine's `Init` asserts `IsInGameThread()`.
`GenerateFaceAnimation` does not, and is where all the time goes, so that is what moves onto a worker
thread. The sound wave is held with a `TStrongObjectPtr` for the duration — the solver dereferences it
off-thread, and a weak pointer that survives the check and dies during the read is a crash with no
useful callstack.

**Models stay resident between clips.** They are most of the cost of a single short line. Drop them
with `FaceForge.ReleaseModels` when you are done with a batch.

**Two of the engine functions used here are `UE_INTERNAL`.** `ReplaceHeadGuiControlsWithRaw` and
`GetHeadPoseTransformFromRawControls` are the only route from the solver's head track to a transform.
Used knowingly: reimplementing the control conversion would be a worse bet than depending on something
that breaks loudly at compile time. Head pose is opt-in per clip, so if it does break, nothing else
stops working.

## The mouth-only mask is ours, not the engine's

The engine has a mouth-only control set, but it is expressed in RigLogic **raw** control names while
the solver's output is in **GUI** control names — two different vocabularies, so its list cannot be
applied to this data. This plugin filters the GUI names itself, including jaw and tongue explicitly,
because a mouth that opens without a jaw is not a mouth.

## Requires

The `MetaHuman` plugin (for the solver and its models) and `StreamingADA` (whose `SpeechAnimationSolver`
module owns the mood enum the solver's public header pulls in). Both are enabled by this plugin's
`.uplugin`. Enabling them needs an editor restart.

If the models are missing, `IsAvailable` says so in words rather than failing at link time — which is
what makes it a message a user can act on.
