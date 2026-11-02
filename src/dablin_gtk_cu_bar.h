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

#ifndef DABLIN_GTK_CU_BAR_H_
#define DABLIN_GTK_CU_BAR_H_

#include <map>
#include <vector>

#include <gtkmm.h>

#include "fic_decoder.h"


// --- DABlinGTKCUBar -----------------------------------------------------------------
// full width bar showing the CIF's 864 Capacity Units, with one block per occupied sub-channel -
// including sub-channels with no (audio) service dablin can play (e.g. data/EPG services),
// so the bar's background accurately reflects which CUs are actually free
class DABlinGTKCUBar : public Gtk::DrawingArea {
private:
	struct Entry {
		int subchid;
		int sid;	// 16-bit (programme) or 32-bit (data) SId
		bool has_sid;	// sid is only meaningful if true (always true when is_audio)
		int scids;
		size_t start;
		size_t size;
		bool is_audio;
	};

	std::vector<LISTED_SERVICE> services;
	std::map<int, FIC_SUBCHANNEL> subchannels;

	std::vector<Entry> entries;
	int selected_subchid;

	void RebuildEntries();
	const Entry* GetEntryAt(double x) const;
	bool on_draw(const Cairo::RefPtr<Cairo::Context>& cr) override;
	bool on_button_press_event(GdkEventButton* event) override;
	bool on_query_tooltip(int x, int y, bool keyboard_tooltip, const Glib::RefPtr<Gtk::Tooltip>& tooltip);
public:
	static const size_t total_cus = 864;

	DABlinGTKCUBar();

	void SetServices(const std::vector<LISTED_SERVICE>& services);
	void SetSubchannels(const std::map<int, FIC_SUBCHANNEL>& subchannels);
	void SetSelectedSubchId(int subchid);

	// emitted with (sid, scids) when the user clicks a service's block
	sigc::signal<void, uint16_t, int> signal_service_clicked;
};

#endif /* DABLIN_GTK_CU_BAR_H_ */
