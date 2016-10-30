/*
 * nxgi_geometry.h
 *
 *  Created on: 7.10.2016 ã.
 *      Author: Anton Angelov
 */

#ifndef SUBSYSTEMS_NXGI_GEOMETRY_H_
#define SUBSYSTEMS_NXGI_GEOMETRY_H_

#include "nxgi.h"

/*
 * Geometry macros
 */
#define RECT_WIDTH(rect) (rect.x2 - rect.x1)
#define RECT_HEIGHT(rect) (rect.y2 - rect.y1)
#define RECT_SIZE(rect) (NXGI_SIZE)(rect.x2 - rect.x1, rect.y2 - rect.y1)

BOOL 		nxgig_point_in_rect(NXGI_POINT p, NXGI_RECT r);
NXGI_POINT	nxgig_clip_point(NXGI_POINT p, NXGI_RECT r);
NXGI_POINT	nxgig_point_add(NXGI_POINT p1, NXGI_POINT p2);
NXGI_POINT	nxgig_point_sub(NXGI_POINT p1, NXGI_POINT p2);

BOOL		nxgig_rect_contains_rect(NXGI_RECT containee, NXGI_RECT container);
BOOL		nxgig_rect_intersect(NXGI_RECT r1, NXGI_RECT r2);
BOOL		nxgig_line_rect_intersect(NXGI_POINT l1, NXGI_POINT l2, NXGI_RECT r2);
BOOL		nxgig_segment_segment_intersect(NXGI_POINT p1, NXGI_POINT q1, NXGI_POINT p2, NXGI_POINT q2);
NXGI_RECT	nxgig_rects_union(NXGI_RECT r1, NXGI_RECT r2);
NXGI_RECT	nxgig_rect_inflate(NXGI_RECT r, int32_t v);
NXGI_RECT	nxgig_rect_offset(NXGI_RECT r, NXGI_POINT vector);

/**
 * Returns the area of the rectangle in square units.
 */
uint32_t	nxgig_rect_area(NXGI_RECT r);

#endif /* SUBSYSTEMS_NXGI_GEOMETRY_H_ */
