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

#ifndef EDI_PFT_H_
#define EDI_PFT_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>


// --- PFTFragment -----------------------------------------------------------------
// One parsed PFT fragment (ETSI TS 102 821 Annex B.1) - the "PF"-prefixed framing
// ODR-DabMux uses for EDI/UDP to keep AF packets within the UDP MTU, optionally
// protected by interleaved Reed-Solomon FEC.
struct PFTFragment {
	uint16_t pseq;
	uint32_t findex;	// 24 bits
	uint32_t fcount;	// 24 bits
	bool fec;
	bool addr;
	uint16_t plen;		// 14 bits
	uint8_t rsk;		// only meaningful if fec
	uint8_t rsz;		// only meaningful if fec
	std::vector<uint8_t> payload;

	// parses exactly one fragment out of a single, complete UDP datagram
	static bool Parse(const uint8_t *data, size_t len, PFTFragment& out);
};


// --- PFTReassembler -----------------------------------------------------------------
// Buffers PFT fragments per Pseq and reassembles the original AF packet, recovering
// missing fragments via RS(255,207) erasure decoding when the sender has FEC enabled.
class PFTReassembler {
public:
	PFTReassembler();
	~PFTReassembler();

	// feeds one fragment; returns true and fills out_af_packet if this completed an AF packet
	bool PushFragment(const PFTFragment& frag, std::vector<uint8_t>& out_af_packet);

private:
	struct PseqState {
		uint32_t fcount;
		bool fec;
		uint16_t plen;
		uint8_t rsk;
		uint8_t rsz;
		std::map<uint32_t, std::vector<uint8_t>> fragments;	// findex -> payload
	};

	std::map<uint16_t, PseqState> pseqs;
	void *rs_handle;

	bool ReassembleFEC(const PseqState& state, std::vector<uint8_t>& out_af_packet);
	void Prune(uint16_t current_pseq);
};

#endif /* EDI_PFT_H_ */
