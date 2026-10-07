// Drives the one-action MARS (coq/Examples/Mars/Spec.v) over MMIO through the
// sequence of sim/tb_mars_v4.sv in the Trustformer repo, and checks the TCG C
// reference emulator's values for it -- the same expected digests that
// testbench carries.
//
// MMIO map -- generated from the module's external functions, see
// generators/trustformer/src/main/resources/regmap/Example_Mars.json. The
// Primary Seed and the init request are not on it: Example_MarsPlatform drives
// them.
#include <stdio.h>
#include "mmio.h"

#define TF_BASE        0x4000UL
#define TF_STATUS      (TF_BASE + 0x00)  // RO: bit0 = busy
#define TF_CMD         (TF_BASE + 0x04)  // WO: 16-bit command code
#define TF_IN_CTX      (TF_BASE + 0x08)  // RW: 8 words
#define TF_IN_CTXLEN   (TF_BASE + 0x28)
#define TF_IN_DIG      (TF_BASE + 0x2C)  // RW: 8 words
#define TF_IN_IDX      (TF_BASE + 0x4C)
#define TF_IN_NLEN     (TF_BASE + 0x50)
#define TF_IN_NONCE    (TF_BASE + 0x54)  // RW: 8 words
#define TF_IN_REGSEL   (TF_BASE + 0x78)
#define TF_OUT_DOUT    (TF_BASE + 0x80)  // RO: 8 words
#define TF_OUT_FAILURE (TF_BASE + 0xA0)
#define TF_OUT_RC      (TF_BASE + 0xE4)
#define TF_OUT_ST      (TF_BASE + 0x108)

// fs_action_encoding in coq/Examples/Mars/Spec.v
#define CC_PCREXTEND 5
#define CC_REGREAD   6
#define CC_QUOTE     10
#define CC_INIT      0xFFFF

#define RC_SUCCESS 0
#define RC_VALUE   6

// A 256-bit value is 8 words; word k holds bits [32k+31 : 32k].
typedef struct { uint32_t w[8]; } u256;

// The TCG C reference emulator's output for this stimulus (tb_mars_v4.sv).
static const char *E_ZERO = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *E_EXT[3] = {
  "90f4b39548df55ad6187a1d20d731ecee78c545b94afd16f42ef7592d99cd365",
  "9dea5804aca8b476cf8f1efb4fe41abae758ccb238d6656dbc4ca5d40803dc74",
  "05937d0339976abf003ab9e1247e5e6e3e06c07b11dceb9c6e333c7569ed0f33",
};
static const char *E_PCR1 = "17eaf835d8496ed16d40454b53344de18ffac7e5fbbb87860889922e51f47d70";
static const char *E_SIG  = "335d2b4259e5011ff8fc5584037939b3a751ce2c842432029f3c00174062ab4f";

static const char *NONCE = "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20";
static const char *CTX   = "2122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f40";

static int checks = 0, failures = 0;

// 64 hex digits, most significant first, as in a Verilog literal.
static u256 hex(const char *s)
{
  u256 v;
  for (int k = 0; k < 8; k++) {
    uint32_t w = 0;
    for (int i = 0; i < 8; i++) {
      char c = s[64 - 8 * (k + 1) + i];
      w = (w << 4) | (uint32_t)(c <= '9' ? c - '0' : c - 'a' + 10);
    }
    v.w[k] = w;
  }
  return v;
}

static u256 small(uint32_t x)
{
  u256 v = {{0}};
  v.w[0] = x;
  return v;
}

static void write256(uintptr_t addr, u256 v)
{
  for (int k = 0; k < 8; k++)
    reg_write32(addr + 4 * k, v.w[k]);
}

static u256 read256(uintptr_t addr)
{
  u256 v;
  for (int k = 0; k < 8; k++)
    v.w[k] = reg_read32(addr + 4 * k);
  return v;
}

static void print256(u256 v)
{
  for (int k = 7; k >= 0; k--)
    printf("%08x", v.w[k]);
}

static void check256(const char *what, u256 got, const char *want_hex)
{
  u256 want = hex(want_hex);
  int same = 1;
  for (int k = 0; k < 8; k++)
    same &= got.w[k] == want.w[k];
  checks++;
  if (!same) {
    failures++;
    printf("  FAIL %s\n       got  ", what);
    print256(got);
    printf("\n       want ");
    print256(want);
    printf("\n");
  } else {
    printf("  ok   %s\n", what);
  }
}

static void check(const char *what, uint32_t got, uint32_t want)
{
  checks++;
  if (got != want) {
    failures++;
    printf("  FAIL %s: got %u, expected %u\n", what, got, want);
  } else {
    printf("  ok   %s = %u\n", what, got);
  }
}

static void wait_idle(void)
{
  while (reg_read32(TF_STATUS) & 1)
    ;
}

// Arguments are already written: the module latches them when it accepts the
// command. One command is one action; it returns once the action retires.
static void issue(uint32_t code)
{
  wait_idle();
  reg_write32(TF_CMD, code);
  wait_idle();
}

static void pcr_extend(uint32_t idx, u256 dig)
{
  wait_idle();
  reg_write32(TF_IN_IDX, idx);
  write256(TF_IN_DIG, dig);
  issue(CC_PCREXTEND);
}

static u256 reg_read(uint32_t idx)
{
  wait_idle();
  reg_write32(TF_IN_IDX, idx);
  issue(CC_REGREAD);
  return read256(TF_OUT_DOUT);
}

int main(void)
{
  printf("mars: status register reads 0x%02x, st=%u before any command\n",
         reg_read32(TF_STATUS), reg_read32(TF_OUT_ST));

  // _MARS_Init: one action, the DP derivation included. The platform holds
  // init_req high until the first one completes.
  printf("_MARS_Init\n");
  issue(CC_INIT);
  check("Init rc", reg_read32(TF_OUT_RC), RC_SUCCESS);
  check("st after Init", reg_read32(TF_OUT_ST), 1);

  printf("fresh PCRs\n");
  check256("PCR0 is zero", reg_read(0), E_ZERO);
  check("RegRead rc", reg_read32(TF_OUT_RC), RC_SUCCESS);
  check256("PCR1 is zero", reg_read(1), E_ZERO);
  check("RegRead rc", reg_read32(TF_OUT_RC), RC_SUCCESS);

  for (uint32_t i = 1; i <= 3; i++) {
    printf("PcrExtend(0, %u)\n", i);
    pcr_extend(0, small(i));
    check("PcrExtend rc", reg_read32(TF_OUT_RC), RC_SUCCESS);
    check256("PCR0 after extend", reg_read(0), E_EXT[i - 1]);
  }

  printf("PcrExtend(1, 0xAA)\n");
  pcr_extend(1, small(0xAA));
  check256("PCR1 after its own extend", reg_read(1), E_PCR1);
  check256("PCR0 unchanged by the PCR1 extend", reg_read(0), E_EXT[2]);

  // The signature is HMAC(AK, snapshot) with AK derived from DP, so one value
  // exercises the whole key hierarchy.
  printf("Quote(regsel=3)\n");
  wait_idle();
  reg_write32(TF_IN_REGSEL, 3);
  write256(TF_IN_NONCE, hex(NONCE));
  write256(TF_IN_CTX, hex(CTX));
  reg_write32(TF_IN_NLEN, 32);
  reg_write32(TF_IN_CTXLEN, 32);
  issue(CC_QUOTE);
  check("Quote rc", reg_read32(TF_OUT_RC), RC_SUCCESS);
  check256("Quote signature", read256(TF_OUT_DOUT), E_SIG);

  // The boot-sequence rule: init_req is low once st is 1, so software cannot
  // re-initialize and wipe the PCRs.
  printf("_MARS_Init again\n");
  issue(CC_INIT);
  check("second Init refused, rc", reg_read32(TF_OUT_RC), RC_VALUE);
  check("st", reg_read32(TF_OUT_ST), 1);
  check("failure", reg_read32(TF_OUT_FAILURE), 0);
  check256("PCR0 survives the refused Init", reg_read(0), E_EXT[2]);

  if (failures) {
    printf("mars: %d OF %d CHECKS FAILED\n", failures, checks);
    return 1;
  }
  printf("mars: all %d checks passed\n", checks);
  return 0;
}
