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

#ifndef EDI_NETWORK_SOURCE_H_
#define EDI_NETWORK_SOURCE_H_

#include "edi_source.h"
#include "edi_pft.h"


// --- EDITCPSource -----------------------------------------------------------------
// Connects to a remote EDI/TCP server and feeds the byte stream through EDISource's
// existing AF packet framing - equivalent to piping `nc host port | dablin -f edi`,
// just with dablin owning the socket itself.
class EDITCPSource : public EDISource {
private:
	std::string host;
	int port;

protected:
	void Init();
	void PrintSource();

public:
	EDITCPSource(std::string host, int port, EnsembleSourceObserver *observer);
	~EDITCPSource() {}
};


// --- EDIUDPSource -----------------------------------------------------------------
// Receives EDI/UDP datagrams (unicast, or multicast when "bind_addr" is a multicast
// address). Each datagram carries either a complete AF packet, or a PFT fragment
// (ETSI TS 102 821 Annex B) which is reassembled via PFTReassembler, including
// RS(255,207) erasure recovery for missing fragments when the sender has FEC enabled.
class EDIUDPSource : public EnsembleSource {
private:
	std::string bind_addr;
	int port;
	PFTReassembler pft;

	bool CheckFrameCompleted(const SYNC_MAGIC&) {return true;}	// unused, Main() is fully overridden

public:
	EDIUDPSource(std::string bind_addr, int port, EnsembleSourceObserver *observer);
	~EDIUDPSource() {}

	int Main();
};

#endif /* EDI_NETWORK_SOURCE_H_ */
