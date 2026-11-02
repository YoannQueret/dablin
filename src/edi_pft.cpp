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

#include "edi_pft.h"

#include <algorithm>

extern "C" {
#include <fec.h>
}

#include "tools.h"


// --- PFTFragment -----------------------------------------------------------------
bool PFTFragment::Parse(const uint8_t *data, size_t len, PFTFragment& out) {
	if(len < 14)
		return false;
	if(!(data[0] == 'P' && data[1] == 'F'))
		return false;

	size_t index = 2;
	out.pseq = data[index] << 8 | data[index + 1];
	index += 2;
	out.findex = data[index] << 16 | data[index + 1] << 8 | data[index + 2];
	index += 3;
	out.fcount = data[index] << 16 | data[index + 1] << 8 | data[index + 2];
	index += 3;

	uint16_t flags_plen = data[index] << 8 | data[index + 1];
	out.fec = flags_plen & 0x8000;
	out.addr = flags_plen & 0x4000;
	out.plen = flags_plen & 0x3FFF;
	index += 2;

	if(out.fcount == 0 || out.findex >= out.fcount)
		return false;

	size_t required_len = index + (out.fec ? 2 : 0) + (out.addr ? 4 : 0) + 2;	// + HCRC
	if(len < required_len)
		return false;

	out.rsk = out.rsz = 0;
	if(out.fec) {
		out.rsk = data[index++];
		out.rsz = data[index++];
	}
	if(out.addr)
		index += 4;	// source/dest port - unused (ODR-DabMux never sets this)

	uint16_t crc_stored = data[index] << 8 | data[index + 1];
	uint16_t crc_calced = CalcCRC::CalcCRC_CRC16_CCITT.Calc(data, index);
	index += 2;

	// the HCRC is only mandatory to pass when FEC is enabled (matches reference decoders)
	if(out.fec && crc_stored != crc_calced)
		return false;

	if(len < index + out.plen)
		return false;

	out.payload.assign(data + index, data + index + out.plen);
	return true;
}


// --- PFTReassembler -----------------------------------------------------------------
PFTReassembler::PFTReassembler() {
	// RS(255,207), as used by ODR-DabMux's PFT FEC (ETSI TS 102 821 Annex B.6/B.7) -
	// a second, independent handle from the one dabplus_decoder.cpp uses for DAB+ RS,
	// since fcr/nroots/pad differ
	rs_handle = init_rs_char(8, 0x11d, 1, 1, 48, 0);
}

PFTReassembler::~PFTReassembler() {
	if(rs_handle)
		free_rs_char(rs_handle);
}

bool PFTReassembler::PushFragment(const PFTFragment& frag, std::vector<uint8_t>& out_af_packet) {
	std::map<uint16_t, PseqState>::iterator it = pseqs.find(frag.pseq);
	if(it != pseqs.end()) {
		PseqState& existing = it->second;
		bool consistent = existing.fcount == frag.fcount && existing.fec == frag.fec &&
				(!frag.fec || (existing.plen == frag.plen && existing.rsk == frag.rsk && existing.rsz == frag.rsz));
		if(!consistent) {
			// inconsistent re-announcement of this Pseq (e.g. a restarted/different sender) - start over
			pseqs.erase(it);
			it = pseqs.end();
		}
	}

	if(it == pseqs.end()) {
		PseqState state;
		state.fcount = frag.fcount;
		state.fec = frag.fec;
		state.plen = frag.plen;
		state.rsk = frag.rsk;
		state.rsz = frag.rsz;
		it = pseqs.insert(std::make_pair(frag.pseq, state)).first;
	}

	PseqState& state = it->second;
	state.fragments[frag.findex] = frag.payload;

	bool completed;
	if(!state.fec) {
		// without FEC, nothing can be done about missing fragments - need them all
		completed = state.fragments.size() == state.fcount;
		if(completed) {
			out_af_packet.clear();
			for(uint32_t findex = 0; findex < state.fcount; findex++) {
				const std::vector<uint8_t>& payload = state.fragments[findex];
				out_af_packet.insert(out_af_packet.end(), payload.begin(), payload.end());
			}
		}
	} else {
		completed = ReassembleFEC(state, out_af_packet);
	}

	if(completed)
		pseqs.erase(it);
	else
		Prune(frag.pseq);

	return completed;
}

bool PFTReassembler::ReassembleFEC(const PseqState& state, std::vector<uint8_t>& out_af_packet) {
	const size_t rsk = state.rsk;
	if(rsk == 0 || rsk > 207)
		return false;
	const size_t chunk_size = rsk + 48;

	// Plen*Fcount is the fragments' total capacity, which the encoder's interleaved
	// fragmentation can slightly overshoot (zero-padding the last fragment, PFT.cpp:182);
	// the true RS block length is always an exact multiple of chunk_size, so flooring
	// recovers it correctly, as long as that padding (< Fcount) stays under one chunk
	size_t rs_block_len_padded = (size_t) state.plen * state.fcount;
	size_t num_chunks = rs_block_len_padded / chunk_size;
	if(num_chunks == 0)
		return false;
	size_t rs_block_len = num_chunks * chunk_size;

	std::vector<uint8_t> af_packet;
	af_packet.reserve(rsk * num_chunks);

	std::vector<uint8_t> codeword(255);
	std::vector<int> eras_pos;
	eras_pos.reserve(48);

	for(size_t chunk_ix = 0; chunk_ix < num_chunks; chunk_ix++) {
		std::fill(codeword.begin(), codeword.end(), 0);
		eras_pos.clear();

		for(size_t off = 0; off < chunk_size; off++) {
			size_t pos = chunk_ix * chunk_size + off;	// byte position within the (virtual) RS block
			if(pos >= rs_block_len)
				break;

			// inverse of the encoder's column-wise interleaving: fragments[findex][k] = rs_block[k*Fcount + findex]
			size_t k = pos / state.fcount;
			uint32_t findex = pos % state.fcount;

			size_t codeword_pos = off < rsk ? off : 207 + (off - rsk);

			std::map<uint32_t, std::vector<uint8_t>>::const_iterator frag_it = state.fragments.find(findex);
			if(frag_it == state.fragments.end() || k >= frag_it->second.size())
				eras_pos.push_back((int) codeword_pos);
			else
				codeword[codeword_pos] = frag_it->second[k];
		}

		if(eras_pos.size() > 48)
			return false;	// beyond RS(255,207)'s correction capability - not enough fragments (yet)

		int result = decode_rs_char(rs_handle, codeword.data(), eras_pos.data(), (int) eras_pos.size());
		if(result < 0)
			return false;	// uncorrectable

		af_packet.insert(af_packet.end(), codeword.begin(), codeword.begin() + rsk);
	}

	// trim the zero padding appended to the last chunk's data at encode time
	if(state.rsz > 0) {
		if(af_packet.size() < state.rsz)
			return false;
		af_packet.resize(af_packet.size() - state.rsz);
	}

	out_af_packet = std::move(af_packet);
	return true;
}

void PFTReassembler::Prune(uint16_t current_pseq) {
	const size_t max_pending = 16;
	if(pseqs.size() <= max_pending)
		return;

	// bound memory under sustained loss/garbage by dropping everything but the Pseq just touched
	for(std::map<uint16_t, PseqState>::iterator it = pseqs.begin(); it != pseqs.end();) {
		if(it->first != current_pseq)
			it = pseqs.erase(it);
		else
			++it;
	}
}
