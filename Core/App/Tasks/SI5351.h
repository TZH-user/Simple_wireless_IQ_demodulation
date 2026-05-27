#ifndef APP_SI5351_H
#define APP_SI5351_H

#include <stdbool.h>

bool app_si5351_is_clock_ready(void);
void app_si5351_request_recovery(void);

#endif /* APP_SI5351_H */
