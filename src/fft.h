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

#ifndef FFT_H_
#define FFT_H_

#include <complex>
#include <vector>

namespace FFT {

// in-place iterative radix-2 Cooley-Tukey FFT; a.size() must be a power of two
void Transform(std::vector<std::complex<double>>& a);

}

#endif /* FFT_H_ */
