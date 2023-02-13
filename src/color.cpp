#include "color.hpp"
#include <cmath>

/* From: http://www.compuphase.com/cmetric.htm */

double ColourDistance(RGB_u e1, RGB_u e2){
	long rmean = ( (long)e1.c.r + (long)e2.c.r ) / 2;
	long r = (long)e1.c.r - (long)e2.c.r;
	long g = (long)e1.c.g - (long)e2.c.g;
	long b = (long)e1.c.b - (long)e2.c.b;
	return std::sqrt((((512+rmean)*r*r)>>8) + 4*g*g + (((767-rmean)*b*b)>>8));
}
