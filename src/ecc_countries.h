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

#ifndef ECC_COUNTRIES_H_
#define ECC_COUNTRIES_H_

#include <cstdint>

// Looks up the ITU country name for an (ECC, SId country ID) pair, per ETSI TS 101 756.
// country_id is the top nibble of the 16-bit SId. Returns nullptr if unknown.
const char* LookupECCCountry(uint8_t ecc, uint8_t country_id);

#endif /* ECC_COUNTRIES_H_ */
