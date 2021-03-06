/*
 * libresdet - Detect source resolution of upscaled images.
 * Copyright (C) 2012-2017 0x09.net
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <math.h>
#include <float.h>

#include "resdet_internal.h"

#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))

// Sweeps the image looking for boundaries with many sign inversions.
// Fastest, simplest, and conveniently one of the most accurate methods.
static RDError detect_method_sign(const coeff* restrict f, size_t length, size_t n, size_t stride, size_t dist, size_t range, double* result, rdint_index* restrict start, rdint_index* restrict end) {
	for(rdint_index x = *start; x < *end; x++) {
		rdint_storage sign_diff = 0;
		for(rdint_index y = 0; y < n; y++)
			for(rdint_index i = 1; i <= range; i++)
				sign_diff += signbit(f[y*stride+x*dist-i*dist]) != signbit(f[y*stride+x*dist+i*dist]);
		result[x-*start] += sign_diff / (double)(n*range);
	}
	return RDEOK;
}

// Looks for similar magnitude coefficients with inverted signs.
static RDError detect_method_magnitude(const coeff* restrict f, size_t length, size_t n, size_t stride, size_t dist, size_t range, double* result, rdint_index* restrict start, rdint_index* restrict end) {
	for(rdint_index x = *start; x < *end; x++) {
		rdint_storage mag_match = 0;
		for(rdint_index y = 0; y < n; y++)
			for(rdint_index i = 1; i <= range; i++) {
				int e;
				mi(frexp)(f[y*stride+x*dist-i*dist]/mc(copysign)(MAX(mc(fabs)(f[y*stride+x*dist+i*dist]),EPSILON),f[y*stride+x*dist+i*dist])+1,&e);
				mag_match += e <= 0;
			}
		result[x-*start] += mag_match / (double)(n*range);
	}
	return RDEOK;
}

// Initial algorithm. Somewhat more complicated mixture of the previous.
// Tests for inverted sign, same magnitude, with lower magnitude/zero crossing in between.
static RDError detect_method_original(const coeff* restrict f, size_t length, size_t n, size_t stride, size_t dist, size_t range, double* result, rdint_index* restrict start, rdint_index* restrict end) {
#ifndef MAG_RANGE
#define MAG_RANGE range
#endif

	rdint_index maxrange = MAX(MAG_RANGE,range);
	if(maxrange*2 >= length)
		return RDEOK; //can't do anything

	RDError ret = RDEOK;
	intermediate* sum = NULL; rdint_storage* sign = NULL;
	if(!(sum = calloc(length,sizeof(*sum)))) { ret = RDENOMEM; goto end; }
	if(!(sign = calloc(length-range*2,sizeof(*sign)))) { ret = RDENOMEM; goto end; }

	*start = maxrange;
	*end = length-maxrange;
	for(rdint_index y = 0; y < n; y++) {
		for(rdint_index x = 0; x < length; x++)
			sum[x] += mi(fabs)(f[y*stride+x*dist]);
		for(rdint_index x = range; x < length-range; x++)
			for(rdint_index i = 1; i <= range; i++)
				sign[x-range] += signbit(f[y*stride+x*dist-i*dist]) != signbit(f[y*stride+x*dist+i*dist]);
	}
	for(rdint_index x = 0; x < length; x++)
		sum[x] /= n;
	for(rdint_index x = maxrange; x < length-maxrange; x++) {
		intermediate left = 0, right = 0, mid = sum[x] * MAG_RANGE;
		for(rdint_index i = 1; i <= MAG_RANGE; i++) {
			left += sum[x-i];
			right += sum[x+i];
		}
		int leftexp, rightexp, midexp;
		mi(frexp)(left,&leftexp); mi(frexp)(right,&rightexp); mi(frexp)(mid,&midexp);
		if((abs(leftexp - rightexp) < 2) && (!mid || MIN(leftexp,rightexp) >= midexp) && (MIN(left,right) > mid) &&
		   (mi(fabs)(left - right) < mi(fabs)(left - mid) && mi(fabs)(left - right) < mi(fabs)(right - mid)))
			result[x-*start] += sign[x-range]/(double)(range*n);
	}

end:
	free(sum);
	free(sign);
	return ret;
}

// A few different attempts to ID the zero-crossing on an odd-ish extension of a function, of many
// Someone with a better math background may certainly know a better one
static RDMethod methods[] = {
	{
		.name = "sign",
		.func = (void(*)(void))detect_method_sign,
		.threshold = 0.55
	},{
		.name = "mag",
		.func = (void(*)(void))detect_method_magnitude,
		.threshold = 0.40
	},{
		.name = "orig",
		.func = (void(*)(void))detect_method_original,
		.threshold = 0.64
	},
	{0}
};

RDMethod* resdet_methods() {
	return methods;
}
