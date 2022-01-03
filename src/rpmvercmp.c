#include "posix.h"

#include <assert.h>
#include <ctype.h>
#include <string.h>

#include "log.h"
#include "rpmvercmp.h"

/*
 * rpmvercmp has been adapted from the rpm source located at rpmio/rpmvercmp.c
 * It was most recently updated against rpm version 4.17.0. Modifications have
 * been made to make it more consistent with the muon coding style and to
 * remove the unneeded temporary buffers.
 */

/**
 * Compare alpha and numeric segments of two versions.
 * return 1: a is newer than b
 *        0: a and b are the same version
 *       -1: b is newer than a
 */
int8_t
rpmvercmp(const struct str *a, const struct str *b)
{
	int rc;
	bool isnum;
	uint32_t ai = 0, bi = 0,
		 ap = 0, bp = 0,
		 al, bl;

	/* easy comparison to see if versions are identical */
	if (str_eql(a, b)) {
		return 0;
	}

	/* loop through each version segment of str1 and str2 and compare them */
	while (ai < a->len && bi < b->len) {
		while (ai < a->len && !isalnum((int)a->s[ai])) {
			++ai;
		}
		while (bi < b->len && !isalnum((int)b->s[bi])) {
			++bi;
		}

		/* If we ran to the end of either, we are finished with the loop */
		if (!(ai < a->len && bi < b->len)) {
			break;
		}

		ap = ai;
		bp = bi;

		/* grab first completely alpha or completely numeric segment */
		/* leave one and two pointing to the start of the alpha or numeric */
		/* segment and walk ptr1 and ptr2 to end of segment */
		if (isdigit((int)a->s[ap])) {
			while (ap < a->len && isdigit((int)a->s[ap])) {
				++ap;
			}
			while (bp < b->len && isdigit((int)b->s[bp])) {
				++bp;
			}
			isnum = true;
		} else {
			while (ap < a->len && isalpha((int)a->s[ap])) {
				++ap;
			}
			while (bp < b->len && isalpha((int)b->s[bp])) {
				++bp;
			}
			isnum = false;
		}

		/* this cannot happen, as we previously tested to make sure that */
		/* the first string has a non-null segment */
		assert(ai != ap);

		/* take care of the case where the two version segments are */
		/* different types: one numeric, the other alpha (i.e. empty) */
		/* numeric segments are always newer than alpha segments */
		/* XXX See patch #60884 (and details) from bugzilla #50977. */
		if (bi == bp) {
			return isnum ? 1 : -1;
		}

		if (isnum) {
			/* this used to be done by converting the digit segments */
			/* to ints using atoi() - it's changed because long  */
			/* digit segments can overflow an int - this should fix that. */

			/* throw away any leading zeros - it's a number, right? */
			while (a->s[ai] == '0') {
				++ai;
			}
			while (b->s[bi] == '0') {
				++bi;
			}

			/* whichever number has more digits wins */
			if (ap - ai > bp - bi) {
				return 1;
			}
			if (bp - bi > ap - ai) {
				return -1;
			}
		}

		/* memcmp will return which one is greater - even if the two */
		/* segments are alpha or if they are numeric.  don't return  */
		/* if they are equal because there might be more segments to */
		/* compare */
		al = ap - ai;
		bl = bp - bi;

		rc = memcmp(&a->s[ai], &b->s[bi], al < bl ? al : bl);
		if (rc) {
			return rc < 1 ? -1 : 1;
		} else if (al != bl) {
			// emulate strcmp where iff the comparison is ==, the
			// longer string wins.
			return al > bl ? 1 : -1;
		}

		/* restore character that was replaced by null above */
		/* *ptr1 = oldch1; */
		/* one = ptr1; */
		ai = ap;
		/* *ptr2 = oldch2; */
		/* two = ptr2; */
		bi = bp;
	}

	/* this catches the case where all numeric and alpha segments have */
	/* compared identically but the segment separating characters were */
	/* different */

	if (!(ai < a->len) && !(bi < b->len)) {
		return 0;
	}

	/* whichever version still has characters left over wins */
	return !(ai < a->len) ? -1 : 1;
}
