#include "mmio.h"

#define CTR_STATUS 0x4000
#define CTR_CMD 0x4004
#define CTR_ARG 0x4008
#define CTR_RESULT 0x400C

#define CMD_ADD 5
#define CMD_RESET 10

// DOC include start: GCD test
int main(void)
{
  uint32_t status = reg_read8(CTR_STATUS);
  uint32_t hw_result = reg_read32(CTR_RESULT);
  printf("CTR_STATUS: 0x%02x, CTR_RESULT: %u\n", status, hw_result);

  for (int i = 0; i < 3; i++)
  {
    reg_write32(CTR_ARG, 100);
    reg_write32(CTR_CMD, CMD_ADD);

    uint32_t result = reg_read32(CTR_RESULT);
    printf("After add #%d: CTR_RESULT = %u\n", i + 1, result);
  }

  reg_write32(CTR_CMD, CMD_RESET);
  uint32_t reset_result = reg_read32(CTR_RESULT);
  printf("After reset: CTR_RESULT = %u\n", reset_result);

  return 0;
}
// DOC include end: GCD test
