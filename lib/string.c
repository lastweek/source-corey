// Basic string routines.  Not hardware optimized, but not shabby.

#ifdef JOS_KERNEL
#include <kern/lib.h>
#include <inc/error.h>
#endif

#ifdef JOS_USER
#include <inc/lib.h>
#include <inc/error.h>
#endif

#ifdef JOS_KERNEL
char *
strncpy(char *dst, const char *src, size_t n) {
	size_t i;
	char *p = dst;

	for (i = 0; i < n; i++) {
		*dst = *src;
		dst++;
		if (*src) {
			src++; // This does null padding if length of src is less than n
		}
	}

	return p;
}

int
strncmp(const char *p, const char *q, size_t n)
{
	while (n > 0 && *p && *p == *q)
		n--, p++, q++;
	if (n == 0)
		return 0;
	else
		return (int) ((unsigned char) *p - (unsigned char) *q);
}

int
strcmp(const char *p, const char *q)
{
	while (*p && *p == *q)
		p++, q++;
	return (int) ((unsigned char) *p - (unsigned char) *q);
}

char *
strchr(const char *s, int c)
{
	for (; *s; s++)
		if (*s == c)
			return (char *) s;
	return 0;
}

char *
strstr(const char *haystack, const char *needle)
{
    size_t nlen = strlen(needle);

    for (; *haystack; haystack++)
	if (!strncmp(haystack, needle, nlen))
	    return (char *) haystack;

    return 0;
}
#endif

#ifdef JOS_USER
static uint64_t unsigned64_overflow_vals[] = {
	0, 0, 9223372036854775807ULL, 6148914691236517205ULL, 4611686018427387903ULL, 3689348814741910323ULL, 3074457345618258602ULL, 2635249153387078802ULL, 2305843009213693951ULL, 2049638230412172401ULL, 1844674407370955161ULL, 1676976733973595601ULL, 1537228672809129301ULL, 1418980313362273201ULL, 1317624576693539401ULL, 1229782938247303441ULL, 1152921504606846975ULL, 1085102592571150095ULL, 1024819115206086200ULL, 970881267037344821ULL, 922337203685477580ULL, 878416384462359600ULL, 838488366986797800ULL, 802032351030850070ULL, 768614336404564650ULL, 737869762948382064ULL, 709490156681136600ULL, 683212743470724133ULL, 658812288346769700ULL, 636094623231363848ULL, 614891469123651720ULL, 595056260442243600ULL, 576460752303423487ULL, 558992244657865200ULL, 542551296285575047ULL, 527049830677415760ULL
};

int
strtou64(const char *begin, char **endptr, int base, uint64_t *return_value)
{
	register const char *s = begin;
	uint64_t val, overflow_val;
	int overflow_digit, result = -E_INVAL;

	// null pointer
	if (!s)
		goto exit;
	
	// gobble initial whitespace
	while (*s == ' ' || *s == '\t')
		s++;

	// plus sign
	if (*s == '+')
		s++;

	// hex or octal base prefix
	if ((base == 0 || base == 16) && *s == '0' && (s[1] == 'x' || s[1] == 'X'))
		s += 2, base = 16;
	else if (base == 0 && *s == '0')
		base = 8;
	else if (base == 0)
		base = 10;
	else if (base < 2 || base > 36)
		goto exit;

	// overflow detection
	overflow_val = unsigned64_overflow_vals[base];
	overflow_digit = 0xFFFFFFFFFFFFFFFFULL - (overflow_val * base);

	// digits
	val = 0;
	while (1) {
		int dig;

		if (*s >= '0' && *s <= '9')
			dig = *s - '0';
		else if (*s >= 'a' && *s <= 'z')
			dig = *s - 'a' + 10;
		else if (*s >= 'A' && *s <= 'Z')
			dig = *s - 'A' + 10;
		else
			break;
		if (dig >= base)
			break;
		if (result == -E_INVAL)
			result = 0;
		if (val > overflow_val || (val == overflow_val && dig > overflow_digit))
			result = -E_RANGE;
		val = (val * base) + dig;
		s++;
	}

	if (result == -E_INVAL)
		s = begin;
	else if (result == -E_RANGE)
		*return_value = 0xFFFFFFFFFFFFFFFFULL;
	else
		*return_value = val;
 exit:
	if (endptr)
		*endptr = (char *) s;
	return result;
}
#endif
