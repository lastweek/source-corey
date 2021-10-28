#ifndef JOS_MACHINE_COMPILER_H
#define JOS_MACHINE_COMPILER_H

#define __is_array(a) \
  static_assert_zero(!__builtin_types_compatible_p(__typeof(a), __typeof(&a[0])))

#define JSHARED_SECTION ".josmp.share.data"
#define JSHARED_ATTR __attribute__ ((section(JSHARED_SECTION)))
#define JTLS

#endif
