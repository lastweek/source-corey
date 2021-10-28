#ifndef __SMI_TLS_H_
#define __SMI_TLS_H_

#define ___TLS_REF(var,index) __tls_##var[index]
#define ___TLS_DEF(type, var, size) type __tls_##var[size]



#define __TLS_REF(var) ___TLS_REF(var,thread_id())
#define __TLS_DEF(type, var) ___TLS_DEF(type,var,256);


#endif
