#include "color.hpp"
#include <cmath>

/* From: http://www.compuphase.com/cmetric.htm */

double ColourDistance(RGB_u e1, RGB_u e2){
	long rmean = ( (long)e1.r + (long)e2.r ) / 2;
	long r = (long)e1.r - (long)e2.r;
	long g = (long)e1.g - (long)e2.g;
	long b = (long)e1.b - (long)e2.b;
	return std::sqrt((((512+rmean)*r*r)>>8) + 4*g*g + (((767-rmean)*b*b)>>8));
}
