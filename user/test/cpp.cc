extern "C" {
#include <inc/stdio.h>
#include <test.h>
}

#include <exception>

class xdec {
public:
    xdec(int *d) : d_(d) {}
    ~xdec(void) { *d_ = *d_ - 1; }
private:
    int *d_;
};

void
cpp_test(void)
{
    cprintf("Test try/catch\n");
    for (int i = 0; i < 5; ) {
	try {
	    cprintf(" %d\n", i);
	    throw std::exception();
	} catch (std::exception &e) {
	    i++;
	}
    }
    cprintf("Test try/catch done!\n");
    
    cprintf("Test destructors\n");
    int r = 5;
    {
	xdec a(&r);
	xdec b(&r);
	xdec c(&r);
	xdec d(&r);
	xdec e(&r);	
    }
    if (r != 0)
	panic("destructors failed");
}
