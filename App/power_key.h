#ifndef __POWER_KEY_H
#define __POWER_KEY_H

#include <stdbool.h>

/** 创建电源键任务、事件组和 2s 定时器（由 Creator 调用），成功返回 1 */
int power_key_create(void);
/** true 表示正在执行关机流程，其他任务应暂停非关键工作。 */
bool power_key_shutdown_active(void);

#endif
