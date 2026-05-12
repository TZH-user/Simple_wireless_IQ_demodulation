#ifndef APP_DEBUG_CONFIG_H
#define APP_DEBUG_CONFIG_H

/*
 * Stage-1 default: keep the runtime path quiet unless a specific
 * debug stream is explicitly re-enabled for lab diagnostics.
 */
#ifndef ADC_UART_OUTPUT_ENABLE
#define ADC_UART_OUTPUT_ENABLE 0U
#endif

#ifndef ADC_ALGO_LOG_ENABLE
#define ADC_ALGO_LOG_ENABLE 1U
#endif

#ifndef ADC_RANGE_LOG_ENABLE
#define ADC_RANGE_LOG_ENABLE 0U
#endif

#ifndef ADC_HEALTH_LOG_ENABLE
#define ADC_HEALTH_LOG_ENABLE 0U
#endif

#ifndef APP_MODDET_UART_TRACE_ENABLE
#define APP_MODDET_UART_TRACE_ENABLE 0U
#endif

#endif /* APP_DEBUG_CONFIG_H */
