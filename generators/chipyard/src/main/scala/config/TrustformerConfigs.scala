package chipyard

import org.chipsalliance.cde.config.{Config}
import trustformer._

// -----------------------------------
// Configs with Trustformer components
// -----------------------------------

// DOC include start: CustomCounterConfig
class TFCustomCounterConfig extends Config(
  new trustformer.WithCustomCounter() ++ // add a custom counter at address 0x2000 with 32-bit width
  new freechips.rocketchip.rocket.WithNHugeCores(1) ++
  new chipyard.config.AbstractConfig)
// DOC include end: CustomCounterConfig

// DOC include start: LockboxTriesConfig
// NOTE: no `_` in the config class name -- chipyard splits CONFIG on `_` to stack
// config fragments, so `TFExample_LockboxTriesConfig` is looked up as
// `TFExample` ++ `LockboxTriesConfig` and fails with ClassNotFoundException.
// The paper's running example (coq/Examples/LockboxTries.v) as an MMIO peripheral
// at 0x4000, next to a single Rocket core.
class TFLockboxTriesConfig extends Config(
  new trustformer.WithExample_LockboxTries(address=0x4000) ++
  new freechips.rocketchip.rocket.WithNHugeCores(1) ++
  new chipyard.config.AbstractConfig)
// DOC include end: LockboxTriesConfig

// DOC include start: MarsConfig
// The one-action MARS (coq/Examples/Mars/Spec.v) at 0x4000 next to a single
// Rocket core. Its SHA-256 and HMAC IPs, Primary Seed and init request live in
// trustformer.Example_MarsPlatform, off the memory map.
class TFMarsConfig extends Config(
  new trustformer.WithExample_Mars(address=0x4000) ++
  new trustformer.WithMarsSeed() ++
  new freechips.rocketchip.rocket.WithNHugeCores(1) ++
  new chipyard.config.AbstractConfig)
// DOC include end: MarsConfig

// DOC include start: MarsV2Config
// MarsV2 (coq/Examples/MarsV2/Spec.v) at 0x4000 next to a single Rocket core;
// its IPs, Primary Seed, init request and fault input live in
// trustformer.Example_MarsV2Platform, off the memory map.
class TFMarsV2Config extends Config(
  new trustformer.WithExample_MarsV2(address=0x4000) ++
  new trustformer.WithMarsSeed() ++
  new freechips.rocketchip.rocket.WithNHugeCores(1) ++
  new chipyard.config.AbstractConfig)
// DOC include end: MarsV2Config

// DOC include start: MarsV2TinyConfig
// MarsV2 next to a TinyRocket (RV32IMAC, M+U modes with PMP, no FPU, no data cache)
// whose only memory is a 128 KiB scratchpad: the microcontroller-class host for the
// firmware comparison.
class TFMarsV2TinyConfig extends Config(
  new trustformer.WithExample_MarsV2(address=0x4000) ++
  new trustformer.WithMarsSeed() ++
  new freechips.rocketchip.rocket.RocketCoreConfig(_.copy(useUser = true)) ++  // for the PMP gate
  new freechips.rocketchip.rocket.WithL1DCacheSets(2048) ++       // 128 KiB scratchpad
  new testchipip.soc.WithNoScratchpads ++
  new freechips.rocketchip.subsystem.WithIncoherentBusTopology ++
  new freechips.rocketchip.subsystem.WithNBanks(0) ++
  new freechips.rocketchip.subsystem.WithNoMemPort ++
  new freechips.rocketchip.rocket.With1TinyCore ++
  new chipyard.config.AbstractConfig)
// DOC include end: MarsV2TinyConfig
