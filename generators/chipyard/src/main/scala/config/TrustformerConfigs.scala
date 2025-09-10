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
