#ifndef JOS_INC_PAD_H
#define JOS_INC_PAD_H

#define PAD_TYPE(t, ln)							\
	union __attribute__((__packed__,  __aligned__(ln))) {		\
	       t v;							\
	       char __p[ln + (sizeof(t) / ln) * ln];			\
	}

#define PAD(t)								\
	union __attribute__((__packed__,  __aligned__(JOS_PAD))) {	\
	       t v;							\
	       char __p[JOS_PAD + (sizeof(t) / JOS_PAD) * JOS_PAD];	\
	}

#define JOS_PAD	64

#endif
