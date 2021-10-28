#ifndef JOS_INC_CHECKPOINT_H
#define JOS_INC_CHECKPOINT_H

void checkpoint_boot(uint64_t sh_id, uint64_t ps_id);
int  checkpoint_boot_spawn(proc_id_t pid, void (*entry)(uint64_t), uint64_t arg);

#endif
