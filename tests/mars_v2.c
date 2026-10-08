// Drives MarsV2 (coq/Examples/MarsV2/Spec.v) over MMIO through the sequence of
// sim/tb_mars_v2.sv in the Trustformer repo, minus its fault and re-Init parts:
// this SoC ties the fault input low and allows one _MARS_Init per reset. The
// expected values are the TCG reference emulator's, as in that bench.
//
// MMIO map -- generated from the module's external functions, see
// generators/trustformer/src/main/resources/regmap/Example_MarsV2.json.
#include <stdio.h>
#include "mmio.h"

#define TF_BASE          0x4000UL
#define TF_STATUS        (TF_BASE + 0x00)  // RO: bit0 = busy
#define TF_CMD           (TF_BASE + 0x04)  // WO: 16-bit command code
#define TF_IN_CTX        (TF_BASE + 0x08)  // RW: 8 words
#define TF_IN_CTXLEN     (TF_BASE + 0x28)
#define TF_IN_DIG        (TF_BASE + 0x2C)  // RW: 8 words
#define TF_IN_IDX        (TF_BASE + 0x4C)
#define TF_IN_NLEN       (TF_BASE + 0x50)
#define TF_IN_NONCE      (TF_BASE + 0x54)  // RW: 8 words
#define TF_IN_PT         (TF_BASE + 0x74)
#define TF_IN_REGSEL     (TF_BASE + 0x78)
#define TF_IN_RESTRICTED (TF_BASE + 0x7C)
#define TF_IN_SIG        (TF_BASE + 0x80)  // RW: 8 words
#define TF_OUT_CAP       (TF_BASE + 0xA0)
#define TF_OUT_DOUT      (TF_BASE + 0xA4)  // RO: 8 words
#define TF_OUT_FAILURE   (TF_BASE + 0xC4)
#define TF_OUT_PCR0      (TF_BASE + 0xC8)  // RO: 8 words
#define TF_OUT_RC        (TF_BASE + 0x108)
#define TF_OUT_RESULT    (TF_BASE + 0x10C)
#define TF_OUT_SNAP      (TF_BASE + 0x110) // RO: 8 words
#define TF_OUT_ST        (TF_BASE + 0x130)

// fs_action_encoding in coq/Examples/MarsV2/Spec.v
#define CC_SELFTEST         0
#define CC_CAPABILITYGET    1
#define CC_SEQUENCEHASH     2
#define CC_SEQUENCEUPDATE   3
#define CC_SEQUENCECOMPLETE 4
#define CC_PCREXTEND        5
#define CC_REGREAD          6
#define CC_DERIVE           7
#define CC_DPDERIVE         8
#define CC_PUBLICREAD       9
#define CC_QUOTE            10
#define CC_SIGN             11
#define CC_SIGNATUREVERIFY  12
#define CC_INIT             0xFFFF

#define RC_SUCCESS 0
#define RC_BUFFER  4
#define RC_COMMAND 5
#define RC_VALUE   6
#define RC_REG     7

// A 256-bit value is 8 words; word k holds bits [32k+31 : 32k].
typedef struct { uint32_t w[8]; } u256;

// The TCG reference emulator's output for this stimulus (sim/tb_mars_v2.sv,
// scripts/regen-golden.py --design mars_v2).
static const char *E_ZERO     = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *E_EXT1     = "90f4b39548df55ad6187a1d20d731ecee78c545b94afd16f42ef7592d99cd365";
static const char *E_PCR1     = "17eaf835d8496ed16d40454b53344de18ffac7e5fbbb87860889922e51f47d70";
static const char *E_QUOTE[4] = {
  "eb01e0c80afbe12c08171cbc79833ce41e6f80a6ac5791b9e8c6a9e1c7522107",
  "1c8be2509eb6f357c7ed5e0e47e2b8d142060f878cc74f6c97c3d2cf22675ac1",
  "17ba17b2bac9579a0a9bfaacfe4899f601194d027778ca7757a4228e0e023bb5",
  "c3ecb2842c5e21f91cbb5d01380eedc1cee81e61eb929b9fed9bf03098811b3f",
};
static const char *E_SNAP[4] = {
  "387b856e561089f473389b31b9faf54ea4fcea5129a46fd6c3a0626f1a56c6ef",
  "ab9167a2c6d6cd5e5b65e556f246e5188ecb75eeb93448671aa4db4fb090af0d",
  "955fcd52ef0e7e868878f6a279e72623ea7f4e59f819669588066ebac8ddbb1f",
  "bd0e0d246b4898fb2bc653ee68b679bf7711939e180d618082583fd9c4ad4a9b",
};
static const char *E_DERIVE   = "7807f9a3a05ad9967c8574954e504fcdda94b28f2cf4a83375998fed4a1e6c70";
static const char *E_SIGN     = "25a5cd60c50ee23c6a28a5e100ba5b80097d5530a4c8722435bd0ce143c14df2";
static const char *E_QUOTE_DP = "884abc5e8978da18ebc13e060ef12bfa3a7b0b458c620edc12200ccded451965";

// Host arguments, first byte most significant.
static const char *NONCE = "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20";
static const char *CTX   = "2122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f40";
static const char *DIG   = "4142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f60";
static const char *CTX2  = "6162636465666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7f80";

static int checks = 0, failures = 0;

// Core cycles from the command write to idle, per command kind (MMIO included).
static const char *kind_name[] = {"SelfTest", "CapabilityGet", "SequenceHash", "SequenceUpdate",
  "SequenceComplete", "PcrExtend", "RegRead", "Derive", "DpDerive", "PublicRead", "Quote",
  "Sign", "SignatureVerify", "_MARS_Init"};
static uint64_t cyc_min[14], cyc_max[14];

static inline uint64_t rdcycle(void)
{
  uint64_t c;
  asm volatile ("rdcycle %0" : "=r"(c));
  return c;
}

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

static u256 flip0(u256 v)
{
  v.w[0] ^= 1;
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
  }
}

static void check(const char *what, uint32_t got, uint32_t want)
{
  checks++;
  if (got != want) {
    failures++;
    printf("  FAIL %s: got %u, expected %u\n", what, got, want);
  }
}

static void wait_idle(void)
{
  while (reg_read32(TF_STATUS) & 1)
    ;
}

// Arguments are already written: the module latches them when it accepts the
// command. One command is one action; this returns once the action retires.
static void issue(uint32_t code)
{
  wait_idle();
  uint64_t t0 = rdcycle();
  reg_write32(TF_CMD, code);
  wait_idle();
  uint64_t dt = rdcycle() - t0;
  int k = code == CC_INIT ? 13 : (int)code;
  if (k < 14) {
    if (!cyc_min[k] || dt < cyc_min[k]) cyc_min[k] = dt;
    if (dt > cyc_max[k]) cyc_max[k] = dt;
  }
}

static void expect_rc(const char *what, uint32_t want)
{
  check(what, reg_read32(TF_OUT_RC), want);
}

static void reg_read(uint32_t idx)
{
  reg_write32(TF_IN_IDX, idx);
  issue(CC_REGREAD);
}

static void pcr_extend(uint32_t idx, u256 dig)
{
  reg_write32(TF_IN_IDX, idx);
  write256(TF_IN_DIG, dig);
  issue(CC_PCREXTEND);
}

static void quote(uint32_t regsel, uint32_t nlen)
{
  reg_write32(TF_IN_REGSEL, regsel);
  write256(TF_IN_NONCE, hex(NONCE));
  reg_write32(TF_IN_NLEN, nlen);
  write256(TF_IN_CTX, hex(CTX));
  reg_write32(TF_IN_CTXLEN, 32);
  issue(CC_QUOTE);
}

static void quote_ok(uint32_t regsel, const char *esig, const char *esnap)
{
  quote(regsel, 32);
  expect_rc("Quote rc", RC_SUCCESS);
  check256("Quote signature", read256(TF_OUT_DOUT), esig);
  check256("Quote snapshot", read256(TF_OUT_SNAP), esnap);
}

static void derive(uint32_t regsel, uint32_t ctxlen)
{
  reg_write32(TF_IN_REGSEL, regsel);
  write256(TF_IN_CTX, hex(CTX));
  reg_write32(TF_IN_CTXLEN, ctxlen);
  issue(CC_DERIVE);
}

static void dp_derive(uint32_t regsel, uint32_t ctxlen)
{
  reg_write32(TF_IN_REGSEL, regsel);
  write256(TF_IN_CTX, hex(CTX2));
  reg_write32(TF_IN_CTXLEN, ctxlen);
  issue(CC_DPDERIVE);
}

static void sign(uint32_t ctxlen)
{
  write256(TF_IN_CTX, hex(CTX));
  reg_write32(TF_IN_CTXLEN, ctxlen);
  write256(TF_IN_DIG, hex(DIG));
  issue(CC_SIGN);
}

static void verify(uint32_t restricted, u256 dig, u256 sig, uint32_t ctxlen)
{
  reg_write32(TF_IN_RESTRICTED, restricted);
  write256(TF_IN_CTX, hex(CTX));
  reg_write32(TF_IN_CTXLEN, ctxlen);
  write256(TF_IN_DIG, dig);
  write256(TF_IN_SIG, sig);
  issue(CC_SIGNATUREVERIFY);
}

static void cap_get(uint32_t pt)
{
  reg_write32(TF_IN_PT, pt);
  issue(CC_CAPABILITYGET);
}

int main(void)
{
  printf("mars_v2: status 0x%02x, st=%u before any command\n",
         reg_read32(TF_STATUS), reg_read32(TF_OUT_ST));

  printf("before _MARS_Init\n");
  issue(CC_SELFTEST);
  expect_rc("SelfTest before Init", RC_VALUE);
  sign(32);
  expect_rc("Sign before Init", RC_VALUE);
  reg_read(0);
  expect_rc("RegRead before Init", RC_VALUE);
  check("st before Init", reg_read32(TF_OUT_ST), 0);

  printf("_MARS_Init, SelfTest, CapabilityGet, unsupported commands\n");
  issue(CC_INIT);
  expect_rc("Init", RC_SUCCESS);
  check("st after Init", reg_read32(TF_OUT_ST), 1);
  issue(CC_SELFTEST);
  expect_rc("SelfTest", RC_SUCCESS);
  check("SelfTest passes", reg_read32(TF_OUT_FAILURE), 0);
  cap_get(1);
  expect_rc("CapabilityGet PT_PCR", RC_SUCCESS);
  check("PT_PCR", reg_read32(TF_OUT_CAP), 2);
  cap_get(9);
  expect_rc("CapabilityGet PT_ALG_SIGN", RC_SUCCESS);
  check("PT_ALG_SIGN", reg_read32(TF_OUT_CAP), 5);
  cap_get(12);
  expect_rc("CapabilityGet tag 12", RC_VALUE);
  issue(CC_SEQUENCEHASH);
  expect_rc("SequenceHash", RC_COMMAND);
  issue(CC_SEQUENCEUPDATE);
  expect_rc("SequenceUpdate", RC_COMMAND);
  issue(CC_SEQUENCECOMPLETE);
  expect_rc("SequenceComplete", RC_COMMAND);
  issue(CC_PUBLICREAD);
  expect_rc("PublicRead", RC_COMMAND);

  printf("PcrExtend, RegRead\n");
  reg_read(0);
  expect_rc("RegRead 0", RC_SUCCESS);
  check256("fresh PCR0", read256(TF_OUT_DOUT), E_ZERO);
  reg_read(2);
  expect_rc("RegRead 2", RC_REG);
  pcr_extend(0, small(1));
  expect_rc("PcrExtend 0", RC_SUCCESS);
  check256("out_pcr0 after the extend", read256(TF_OUT_PCR0), E_EXT1);
  reg_read(0);
  check256("PCR0", read256(TF_OUT_DOUT), E_EXT1);
  pcr_extend(1, small(0xAA));
  expect_rc("PcrExtend 1", RC_SUCCESS);
  reg_read(1);
  check256("PCR1", read256(TF_OUT_DOUT), E_PCR1);
  check256("PCR0 unchanged by the PCR1 extend", read256(TF_OUT_PCR0), E_EXT1);
  pcr_extend(2, small(1));
  expect_rc("PcrExtend 2", RC_REG);

  printf("Quote over the four regSelect shapes\n");
  for (uint32_t rs = 0; rs < 4; rs++)
    quote_ok(rs, E_QUOTE[rs], E_SNAP[rs]);
  quote(3, 31);
  expect_rc("Quote nlen=31", RC_BUFFER);

  printf("Derive, Sign, SignatureVerify\n");
  derive(3, 32);
  expect_rc("Derive", RC_SUCCESS);
  check256("Derive output", read256(TF_OUT_DOUT), E_DERIVE);
  derive(4, 32);
  expect_rc("Derive regsel=4", RC_REG);
  sign(32);
  expect_rc("Sign", RC_SUCCESS);
  check256("Sign signature", read256(TF_OUT_DOUT), E_SIGN);
  verify(0, hex(DIG), hex(E_SIGN), 32);
  expect_rc("SignatureVerify U, Sign's MAC", RC_SUCCESS);
  check("verdict U, match", reg_read32(TF_OUT_RESULT), 1);
  sign(0);
  expect_rc("Sign ctxlen=0", RC_BUFFER);
  check("an error answer clears the verdict", reg_read32(TF_OUT_RESULT), 0);
  verify(0, hex(DIG), flip0(hex(E_SIGN)), 32);
  expect_rc("SignatureVerify U, one bit flipped", RC_SUCCESS);
  check("verdict U, mismatch", reg_read32(TF_OUT_RESULT), 0);
  verify(1, hex(DIG), hex(E_SIGN), 32);
  check("verdict R on Sign's MAC", reg_read32(TF_OUT_RESULT), 0);
  verify(1, hex(E_SNAP[3]), hex(E_QUOTE[3]), 32);
  check("verdict R on Quote's signature", reg_read32(TF_OUT_RESULT), 1);
  verify(0, hex(DIG), hex(E_SIGN), 31);
  expect_rc("SignatureVerify ctxlen=31", RC_BUFFER);

  printf("_MARS_Init again (refused: once per reset)\n");
  issue(CC_INIT);
  expect_rc("second Init", RC_VALUE);
  check("st", reg_read32(TF_OUT_ST), 1);

  printf("DpDerive\n");
  dp_derive(3, 32);
  expect_rc("DpDerive", RC_SUCCESS);
  quote_ok(3, E_QUOTE_DP, E_SNAP[3]);
  dp_derive(4, 0);
  expect_rc("DpDerive regsel=4 ctxlen=0", RC_REG);
  quote_ok(3, E_QUOTE_DP, E_SNAP[3]);
  dp_derive(3, 0);
  expect_rc("DpDerive ctxlen=0 (DP reset)", RC_SUCCESS);
  quote_ok(3, E_QUOTE[3], E_SNAP[3]);
  check("failure", reg_read32(TF_OUT_FAILURE), 0);

  if (failures)
    printf("mars_v2: %d OF %d CHECKS FAILED\n", failures, checks);
  else
    printf("mars_v2: all %d checks passed\n", checks);

  printf("core cycles per command, write to idle (min..max):\n");
  for (int k = 0; k < 14; k++)
    if (cyc_max[k])
      printf("  %-16s %lu..%lu\n", kind_name[k],
             (unsigned long)cyc_min[k], (unsigned long)cyc_max[k]);

  return failures ? 1 : 0;
}
