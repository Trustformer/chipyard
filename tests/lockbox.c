// Drives the Trustformer "lockbox with a retry counter" (the paper's running
// example, coq/Examples/LockboxTries.v) over MMIO and checks the transcript
// against the Coq `Example`s in that file.
//
// MMIO map -- generated from the module's external functions, see
// generators/trustformer/src/main/resources/regmap/Example_LockboxTries.json
#include <stdio.h>
#include "mmio.h"

#define TF_BASE       0x4000UL
#define TF_STATUS     (TF_BASE + 0x00) // RO: bit0 = busy
#define TF_CMD        (TF_BASE + 0x04) // WO: 16-bit action encoding
#define TF_IN_PIN     (TF_BASE + 0x08) // RW: $fs_in_pin
#define TF_IN_SECRET  (TF_BASE + 0x0C) // RW: $fs_in_secret
#define TF_OUT_STATUS (TF_BASE + 0x10) // RO: $fs_out_status
#define TF_OUT_SECRET (TF_BASE + 0x14) // RO: $fs_out_secret

// fs_action_encoding in coq/Examples/LockboxTries.v
#define CMD_SET  0x0000
#define CMD_TEST 0x000A

#define PIN     10
#define SECRET  42
#define BAD_PIN 7

static int failures = 0;

static void wait_idle(void)
{
  while (reg_read32(TF_STATUS) & 1)
    ;
}

static void check(const char *what, uint32_t got, uint32_t want)
{
  if (got != want) {
    printf("  FAIL %s: got %u, expected %u\n", what, got, want);
    failures++;
  } else {
    printf("  ok   %s = %u\n", what, got);
  }
}

// Write the arguments first, then the command: the module latches its inputs in
// the cycle it accepts the command, so they must already be in place.
static void lockbox_set(uint32_t pin, uint32_t secret)
{
  wait_idle();
  reg_write32(TF_IN_PIN, pin);
  reg_write32(TF_IN_SECRET, secret);
  reg_write32(TF_CMD, CMD_SET);
  wait_idle();
}

static void lockbox_test(uint32_t pin)
{
  wait_idle();
  reg_write32(TF_IN_PIN, pin);
  reg_write32(TF_CMD, CMD_TEST);
  wait_idle();
}

int main(void)
{
  printf("lockbox: status register reads 0x%02x before any command\n",
         reg_read32(TF_STATUS));

  printf("set(pin=%d, secret=%d)\n", PIN, SECRET);
  lockbox_set(PIN, SECRET);
  check("out_status", reg_read32(TF_OUT_STATUS), 0);
  check("out_secret", reg_read32(TF_OUT_SECRET), 0);

  // wrong_reports_failure / wrong_keeps_secret
  printf("test(pin=%d)  -- wrong pin, costs one try\n", BAD_PIN);
  lockbox_test(BAD_PIN);
  check("out_status", reg_read32(TF_OUT_STATUS), 0);
  check("out_secret", reg_read32(TF_OUT_SECRET), 0);

  // right_reports_success / right_releases_secret
  printf("test(pin=%d) -- right pin, releases the secret and refills tries\n", PIN);
  lockbox_test(PIN);
  check("out_status", reg_read32(TF_OUT_STATUS), 1);
  check("out_secret", reg_read32(TF_OUT_SECRET), SECRET);

  // three_wrong_exhausts_tries: tries was refilled to 3 by the success above
  for (int i = 1; i <= 3; i++) {
    printf("test(pin=%d)  -- wrong pin #%d of 3\n", BAD_PIN, i);
    lockbox_test(BAD_PIN);
    check("out_status", reg_read32(TF_OUT_STATUS), 0);
  }

  // locked_out_reports_failure: even the right pin is refused now.
  // out_secret still reads %d from the successful test above -- the output
  // register keeps its last value, it is not re-released here.
  printf("test(pin=%d) -- right pin, but locked out\n", PIN);
  lockbox_test(PIN);
  check("out_status", reg_read32(TF_OUT_STATUS), 0);
  check("out_secret (stale, not re-released)", reg_read32(TF_OUT_SECRET), SECRET);

  // The transcript above does not by itself pin down how many times each command
  // executed: it would read the same if every command ran twice. These two phases
  // do -- `set` refills tries to 3, so with one execution per command two wrong
  // pins leave one try and the third attempt succeeds, while with two executions
  // per command the counter is already exhausted and it fails.
  printf("counting tries: set, two wrong pins, then the right one\n");
  lockbox_set(PIN, SECRET);
  lockbox_test(BAD_PIN);
  lockbox_test(BAD_PIN);
  lockbox_test(PIN);
  check("out_status after 2 wrong (1 try left)", reg_read32(TF_OUT_STATUS), 1);

  printf("counting tries: set, three wrong pins, then the right one\n");
  lockbox_set(PIN, SECRET);
  lockbox_test(BAD_PIN);
  lockbox_test(BAD_PIN);
  lockbox_test(BAD_PIN);
  lockbox_test(PIN);
  check("out_status after 3 wrong (locked out)", reg_read32(TF_OUT_STATUS), 0);

  if (failures) {
    printf("lockbox: %d CHECK(S) FAILED\n", failures);
    return 1;
  }
  printf("lockbox: all checks passed\n");
  return 0;
}
