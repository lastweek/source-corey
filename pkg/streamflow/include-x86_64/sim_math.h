#ifndef __SMI_MATH_H_
#define __SMI_MATH_H_

/**
 * Returns ceil(a/b).
 */
static __inline int sim_ceil(int a, int b)
{
	if (a==0)
	{
		return 0;
	}
	else
	{
		if (a%b==0)
		{
			return a/b;
		}
		else
		{
			return a/b+1;
		}
	}
}
/**
 */
static __inline int min_pow2_value_no_less_than(int a)
{
	int i;
	int v=1;
	for (i=0;;i++)
	{
		if (a<=(v<<i))
			return (v<<i);
	}
	return -1;
}
/**
 */
static __inline int min_pow2_no_less_than(int a)
{
	int i;
	int v=1;
	for (i=0;;i++)
	{
		if (a<=(v<<i))
			return i;
	}
	return -1;
}
#endif
