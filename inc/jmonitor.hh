#ifndef JOS_INC_JMONITOR_HH
#define JOS_INC_JMONITOR_HH

extern "C" {
#include <inc/share.h>
}

class jmonitor {
public:
    jmonitor(void) : dev_ref_(0), n_dev_ref_(0), size_dev_ref_(0) {}

    void add_device(struct sobj_ref ref);
    void start(proc_id_t pid);
    
private:
    struct sobj_ref *dev_ref_;
    uint64_t n_dev_ref_;
    uint64_t size_dev_ref_;
};

#endif
