#include "app_sdram_test.h"
#include "../app_memory_map.h"
#include "main.h"

/*
 * 这块测试区刻意避开当前 LTDC 帧缓冲：
 * - 帧缓冲从 0xC0000000 开始
 * - 800 * 480 * 2 = 768000 bytes，未超过 1MB
 * - 所以第一版测试先从 0xC0100000 开始，避免和显示区重叠
 */
#define APP_SDRAM_TEST_BASE              ((uint32_t)APP_SDRAM_TEST_ADDR)
#define APP_SDRAM_TEST_WORD_COUNT        (APP_SDRAM_TEST_SIZE_BYTES / sizeof(uint16_t))
#define APP_SDRAM_CACHE_LINE_SIZE        32U
#define APP_SDRAM_RETENTION_DELAY_MS     1000U

static uint16_t * const g_sdram_test_mem = (uint16_t *)APP_SDRAM_TEST_BASE;

static void App_SdramTestResetResult(AppSdramTestResult *result);
static void App_SdramTestRecordFailure(AppSdramTestResult *result,
                                       AppSdramTestStatus status,
                                       uint32_t address,
                                       uint16_t expected,
                                       uint16_t actual);
static void App_SdramTestCleanRange(uint32_t addr, uint32_t size);
static void App_SdramTestInvalidateRange(uint32_t addr, uint32_t size);

static AppSdramTestStatus App_SdramTestRunFixedPattern(uint16_t pattern,
                                                       AppSdramTestStatus fail_status,
                                                       AppSdramTestResult *result);
static AppSdramTestStatus App_SdramTestRunAddressPattern(AppSdramTestResult *result);
static AppSdramTestStatus App_SdramTestRunRetentionPattern(AppSdramTestResult *result);

AppSdramTestStatus App_SdramTestRun(AppSdramTestResult *result)
{
  AppSdramTestStatus status = APP_SDRAM_TEST_OK;

  App_SdramTestResetResult(result);

  status = App_SdramTestRunFixedPattern(0x0000U, APP_SDRAM_TEST_FAIL_PATTERN_0000, result);
  if (status != APP_SDRAM_TEST_OK)
  {
    return status;
  }

  status = App_SdramTestRunFixedPattern(0xFFFFU, APP_SDRAM_TEST_FAIL_PATTERN_FFFF, result);
  if (status != APP_SDRAM_TEST_OK)
  {
    return status;
  }

  status = App_SdramTestRunFixedPattern(0xAAAAU, APP_SDRAM_TEST_FAIL_PATTERN_AAAA, result);
  if (status != APP_SDRAM_TEST_OK)
  {
    return status;
  }

  status = App_SdramTestRunFixedPattern(0x5555U, APP_SDRAM_TEST_FAIL_PATTERN_5555, result);
  if (status != APP_SDRAM_TEST_OK)
  {
    return status;
  }

  status = App_SdramTestRunAddressPattern(result);
  if (status != APP_SDRAM_TEST_OK)
  {
    return status;
  }

  status = App_SdramTestRunRetentionPattern(result);
  if (status != APP_SDRAM_TEST_OK)
  {
    return status;
  }

  return APP_SDRAM_TEST_OK;
}

static void App_SdramTestResetResult(AppSdramTestResult *result)
{
  if (result == NULL)
  {
    return;
  }

  result->status = APP_SDRAM_TEST_OK;
  result->fail_address = 0U;
  result->expected = 0U;
  result->actual = 0U;
}

static void App_SdramTestRecordFailure(AppSdramTestResult *result,
                                       AppSdramTestStatus status,
                                       uint32_t address,
                                       uint16_t expected,
                                       uint16_t actual)
{
  if (result == NULL)
  {
    return;
  }

  result->status = status;
  result->fail_address = address;
  result->expected = expected;
  result->actual = actual;
}

static void App_SdramTestCleanRange(uint32_t addr, uint32_t size)
{
  uint32_t aligned_addr = addr & ~(APP_SDRAM_CACHE_LINE_SIZE - 1U);
  uint32_t aligned_size = ((addr + size + APP_SDRAM_CACHE_LINE_SIZE - 1U) &
                           ~(APP_SDRAM_CACHE_LINE_SIZE - 1U)) - aligned_addr;

  SCB_CleanDCache_by_Addr((uint32_t *)aligned_addr, (int32_t)aligned_size);
}

static void App_SdramTestInvalidateRange(uint32_t addr, uint32_t size)
{
  uint32_t aligned_addr = addr & ~(APP_SDRAM_CACHE_LINE_SIZE - 1U);
  uint32_t aligned_size = ((addr + size + APP_SDRAM_CACHE_LINE_SIZE - 1U) &
                           ~(APP_SDRAM_CACHE_LINE_SIZE - 1U)) - aligned_addr;

  SCB_InvalidateDCache_by_Addr((uint32_t *)aligned_addr, (int32_t)aligned_size);
}

static AppSdramTestStatus App_SdramTestRunFixedPattern(uint16_t pattern,
                                                       AppSdramTestStatus fail_status,
                                                       AppSdramTestResult *result)
{
  uint32_t i = 0U;
  uint16_t actual = 0U;
  uint32_t address = 0U;

  /* 先整段写入同一个模式，主要检查数据位是否存在粘连、固定高低等问题。 */
  for (i = 0U; i < APP_SDRAM_TEST_WORD_COUNT; i++)
  {
    g_sdram_test_mem[i] = pattern;
  }

  /*
   * 先 Clean 再 Invalidate：
   * - Clean：确保 CPU 刚写的数据真正回到 SDRAM
   * - Invalidate：确保后面的读取不是直接从旧 DCache 中“测假了”
   */
  App_SdramTestCleanRange(APP_SDRAM_TEST_BASE, APP_SDRAM_TEST_SIZE_BYTES);
  App_SdramTestInvalidateRange(APP_SDRAM_TEST_BASE, APP_SDRAM_TEST_SIZE_BYTES);

  for (i = 0U; i < APP_SDRAM_TEST_WORD_COUNT; i++)
  {
    actual = g_sdram_test_mem[i];
    if (actual != pattern)
    {
      address = APP_SDRAM_TEST_BASE + (i * sizeof(uint16_t));
      App_SdramTestRecordFailure(result, fail_status, address, pattern, actual);
      return fail_status;
    }
  }

  return APP_SDRAM_TEST_OK;
}

static AppSdramTestStatus App_SdramTestRunAddressPattern(AppSdramTestResult *result)
{
  uint32_t i = 0U;
  uint16_t expected = 0U;
  uint16_t actual = 0U;
  uint32_t address = 0U;

  /*
   * 地址模式测试比纯固定值更重要：
   * 如果地址线有镜像、短接、错位，固定值测试可能全过，但这个测试通常会暴露问题。
   */
  for (i = 0U; i < APP_SDRAM_TEST_WORD_COUNT; i++)
  {
    g_sdram_test_mem[i] = (uint16_t)(i & 0xFFFFU);
  }

  App_SdramTestCleanRange(APP_SDRAM_TEST_BASE, APP_SDRAM_TEST_SIZE_BYTES);
  App_SdramTestInvalidateRange(APP_SDRAM_TEST_BASE, APP_SDRAM_TEST_SIZE_BYTES);

  for (i = 0U; i < APP_SDRAM_TEST_WORD_COUNT; i++)
  {
    expected = (uint16_t)(i & 0xFFFFU);
    actual = g_sdram_test_mem[i];

    if (actual != expected)
    {
      address = APP_SDRAM_TEST_BASE + (i * sizeof(uint16_t));
      App_SdramTestRecordFailure(result, APP_SDRAM_TEST_FAIL_ADDRESS, address, expected, actual);
      return APP_SDRAM_TEST_FAIL_ADDRESS;
    }
  }

  return APP_SDRAM_TEST_OK;
}

static AppSdramTestStatus App_SdramTestRunRetentionPattern(AppSdramTestResult *result)
{
  uint32_t i = 0U;
  uint16_t expected = 0U;
  uint16_t actual = 0U;
  uint32_t address = 0U;

  /*
   * 这里测“保持时间”：
   * 写入一个和地址有关的模式后等待一段时间，再读回。
   * 这一步主要帮助发现刷新率、初始化序列或时序边界问题。
   */
  for (i = 0U; i < APP_SDRAM_TEST_WORD_COUNT; i++)
  {
    g_sdram_test_mem[i] = (uint16_t)((i ^ 0x5A5AU) & 0xFFFFU);
  }

  App_SdramTestCleanRange(APP_SDRAM_TEST_BASE, APP_SDRAM_TEST_SIZE_BYTES);

  HAL_Delay(APP_SDRAM_RETENTION_DELAY_MS);

  /* 延时后先失效缓存，确保重新从外部 SDRAM 取数。 */
  App_SdramTestInvalidateRange(APP_SDRAM_TEST_BASE, APP_SDRAM_TEST_SIZE_BYTES);

  for (i = 0U; i < APP_SDRAM_TEST_WORD_COUNT; i++)
  {
    expected = (uint16_t)((i ^ 0x5A5AU) & 0xFFFFU);
    actual = g_sdram_test_mem[i];

    if (actual != expected)
    {
      address = APP_SDRAM_TEST_BASE + (i * sizeof(uint16_t));
      App_SdramTestRecordFailure(result, APP_SDRAM_TEST_FAIL_RETENTION, address, expected, actual);
      return APP_SDRAM_TEST_FAIL_RETENTION;
    }
  }

  return APP_SDRAM_TEST_OK;
}
