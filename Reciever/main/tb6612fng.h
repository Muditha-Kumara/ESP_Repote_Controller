#ifndef TB6612FNG_H
#define TB6612FNG_H

#include "types.h"

void tb6612fng_init(void);
void tb6612fng_apply(const motor_control_t *command);
void tb6612fng_stop(void);

#endif // TB6612FNG_H
