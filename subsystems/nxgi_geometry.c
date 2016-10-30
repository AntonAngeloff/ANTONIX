/*
 * nxgi_geometry.c
 *
 *  Created on: 7.10.2016 ã.
 *      Author: Anton Angelov
 */
#include "nxgi_geometry.h"

NXGI_POINT nxgig_clip_point(NXGI_POINT p, NXGI_RECT r)
{
	if (p.x < r.x1) {
		p.x = r.x1;
	} else if (p.x > r.x2) {
		p.x = r.x2;
	}

	if (p.y < r.y1) {
		p.y = r.y1;
	} else if (p.y > r.y2) {
		p.y = r.y2;
	}

	return p;
}

BOOL nxgig_point_in_rect(NXGI_POINT p, NXGI_RECT r)
{
	return (p.x >= r.x1 && p.x <= r.x2 && p.y >= r.y1 && p.y <= r.y2) ? TRUE : FALSE;
}

BOOL nxgig_rect_intersect(NXGI_RECT r1, NXGI_RECT r2)
{
	return !(r2.x1 > r1.x2 || r2.x2 < r1.x1 ||
	         r2.y1 > r1.y2 || r2.y2 < r1.y1);
}

BOOL nxgig_rect_contains_rect(NXGI_RECT containee, NXGI_RECT container)
{
	return nxgig_point_in_rect(containee.p1, container) && nxgig_point_in_rect(containee.p2, container);
}

NXGI_RECT nxgig_rects_union(NXGI_RECT r1, NXGI_RECT r2)
{
	NXGI_RECT result;

	result.x1 = r1.x1 < r2.x1 ? r1.x1 : r2.x1;
	result.y1 = r1.y1 < r2.y1 ? r1.y1 : r2.y1;
	result.x2 = r1.x2 > r2.x2 ? r1.x2 : r2.x2;
	result.y2 = r1.y2 > r2.y2 ? r1.y2 : r2.y2;

	return result;
}

uint32_t nxgig_rect_area(NXGI_RECT r)
{
	return RECT_WIDTH(r) * RECT_HEIGHT(r);
}

NXGI_RECT nxgig_rect_inflate(NXGI_RECT r, int32_t v)
{
	r.x1 -= v;
	r.y1 -= v;
	r.x2 += v;
	r.y2 += v;

	return r;
}

NXGI_POINT	nxgig_point_add(NXGI_POINT p1, NXGI_POINT p2)
{
	return POINT(p1.x + p2.x, p1.y + p2.y);
}

NXGI_POINT nxgig_point_sub(NXGI_POINT p1, NXGI_POINT p2)
{
	return POINT(p1.x - p2.x, p1.y - p2.y);
}

NXGI_RECT nxgig_rect_offset(NXGI_RECT r, NXGI_POINT vector)
{
	r.p1 = nxgig_point_add(r.p1, vector);
	r.p2 = nxgig_point_add(r.p2, vector);

	return r;
}

BOOL nxgig_line_rect_intersect(NXGI_POINT l1, NXGI_POINT l2, NXGI_RECT r2)
{
	/* Test for intersection with the four corners */
	BOOL edge_intersect =
			(nxgig_segment_segment_intersect(l1, l2, POINT(r2.x1, r2.y1), POINT(r2.x2, r2.y1)) ||
			nxgig_segment_segment_intersect(l1, l2, POINT(r2.x2, r2.y1), POINT(r2.x2, r2.y2)) ||
			nxgig_segment_segment_intersect(l1, l2, POINT(r2.x2, r2.y2), POINT(r2.x1, r2.y2)) ||
			nxgig_segment_segment_intersect(l1, l2, POINT(r2.x1, r2.y2), POINT(r2.x1, r2.y1)));

	BOOL contain =
			nxgig_point_in_rect(l1, r2) && nxgig_point_in_rect(l2, r2);

	return edge_intersect || contain;
}

BOOL helper_on_segment(NXGI_POINT p, NXGI_POINT q, NXGI_POINT r)
{
	#define min(a,b) (a<b ? a : b)
	#define max(a,b) (a>b ? a : b)

    if (q.x <= max(p.x, r.x) && q.x >= min(p.x, r.x) &&
        q.y <= max(p.y, r.y) && q.y >= min(p.y, r.y))
    {
       return TRUE;
    }

    return FALSE;
}

int32_t helper_orientation(NXGI_POINT p, NXGI_POINT q, NXGI_POINT r)
{
	int32_t val = (q.y - p.y) * (r.x - q.x) -
              (q.x - p.x) * (r.y - q.y);

    if (val == 0) {
    	// colinear
    	return 0;
    }

    // clock or counterclock wise
    return (val > 0)? 1: 2;
}

BOOL nxgig_segment_segment_intersect(NXGI_POINT p1, NXGI_POINT q1, NXGI_POINT p2, NXGI_POINT q2)
{
    // Find the four orientations needed for general and
    // special cases
	int32_t o1 = helper_orientation(p1, q1, p2);
	int32_t o2 = helper_orientation(p1, q1, q2);
	int32_t o3 = helper_orientation(p2, q2, p1);
	int32_t o4 = helper_orientation(p2, q2, q1);

    // General case
    if (o1 != o2 && o3 != o4)
        return TRUE;

    // Special Cases
    // p1, q1 and p2 are colinear and p2 lies on segment p1q1
    if (o1 == 0 && helper_on_segment(p1, p2, q1)) return TRUE;

    // p1, q1 and p2 are colinear and q2 lies on segment p1q1
    if (o2 == 0 && helper_on_segment(p1, q2, q1)) return TRUE;

    // p2, q2 and p1 are colinear and p1 lies on segment p2q2
    if (o3 == 0 && helper_on_segment(p2, p1, q2)) return TRUE;

     // p2, q2 and q1 are colinear and q1 lies on segment p2q2
    if (o4 == 0 && helper_on_segment(p2, q1, q2)) return TRUE;

    return FALSE; // Doesn't fall in any of the above cases
}
