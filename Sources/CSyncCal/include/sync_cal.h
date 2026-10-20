#ifndef SYNC_CAL_H
#define SYNC_CAL_H

/* 初始化（展开路径、初始化 curl）。需在使用其它函数前调用一次。 */
void sync_cal_init(void);

/* 执行一次同步：读库、比对快照、上传变更。 */
int sync_cal_do_sync(void);

#endif
