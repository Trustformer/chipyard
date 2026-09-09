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
