#ifndef APP_SDRAM_TEST_H
#define APP_SDRAM_TEST_H

#include <stdint.h>

typedef enum
{
  APP_SDRAM_TEST_OK = 0,
  APP_SDRAM_TEST_FAIL_PATTERN_0000,
  APP_SDRAM_TEST_FAIL_PATTERN_FFFF,
  APP_SDRAM_TEST_FAIL_PATTERN_AAAA,
  APP_SDRAM_TEST_FAIL_PATTERN_5555,
  APP_SDRAM_TEST_FAIL_ADDRESS,
  APP_SDRAM_TEST_FAIL_RETENTION
} AppSdramTestStatus;

typedef struct
{
  AppSdramTestStatus status;
  uint32_t fail_address;
  uint16_t expected;
  uint16_t actual;
} AppSdramTestResult;

AppSdramTestStatus App_SdramTestRun(AppSdramTestResult *result);

#endif /* APP_SDRAM_TEST_H */
