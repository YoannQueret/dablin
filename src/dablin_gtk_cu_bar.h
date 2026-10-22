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

#include <vector>

#include <gtkmm.h>

#include "fic_decoder.h"


// --- DABlinGTKCUBar -----------------------------------------------------------------
// full width bar showing the CIF's 864 Capacity Units, with one block per occupied sub-channel
class DABlinGTKCUBar : public Gtk::DrawingArea {
private:
	struct Entry {
		int subchid;
		size_t start;
		size_t size;
	};

	std::vector<Entry> entries;
	int selected_subchid;

	bool on_draw(const Cairo::RefPtr<Cairo::Context>& cr) override;
public:
	static const size_t total_cus = 864;

	DABlinGTKCUBar();

	void SetServices(const std::vector<LISTED_SERVICE>& services);
	void SetSelectedSubchId(int subchid);
};

#endif /* DABLIN_GTK_CU_BAR_H_ */
