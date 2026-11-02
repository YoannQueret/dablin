/*
    DABlin - capital DAB experience

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "fft.h"

#include <cmath>

namespace FFT {

void Transform(std::vector<std::complex<double>>& a) {
	size_t n = a.size();
	if(n <= 1)
		return;

	// bit-reversal permutation
	for(size_t i = 1, j = 0; i < n; i++) {
		size_t bit = n >> 1;
		for(; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if(i < j)
			std::swap(a[i], a[j]);
	}

	// iterative Cooley-Tukey, butterfly stage by stage
	for(size_t len = 2; len <= n; len <<= 1) {
		double ang = -2 * M_PI / len;
		std::complex<double> wlen(std::cos(ang), std::sin(ang));
		for(size_t i = 0; i < n; i += len) {
			std::complex<double> w(1.0, 0.0);
			for(size_t k = 0; k < len / 2; k++) {
				std::complex<double> u = a[i + k];
				std::complex<double> v = a[i + k + len / 2] * w;
				a[i + k] = u + v;
				a[i + k + len / 2] = u - v;
				w *= wlen;
			}
		}
	}
}

}
