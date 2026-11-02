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

#include "dablin_gtk_cu_bar.h"

#include <algorithm>


// --- DABlinGTKCUBar -----------------------------------------------------------------
DABlinGTKCUBar::DABlinGTKCUBar() : selected_subchid(-1) {
	set_size_request(-1, 44);
	set_hexpand(true);
	set_tooltip_text("Capacity Unit occupation (864 CUs per CIF) - click a service to play it");

	add_events(Gdk::BUTTON_PRESS_MASK);
}

void DABlinGTKCUBar::SetServices(const std::vector<LISTED_SERVICE>& services) {
	entries.clear();

	// one block per distinct sub-channel: a service's secondary components share the
	// same sub-channel (and thus the same CUs) as its primary component
	for(const LISTED_SERVICE& service : services) {
		if(service.subchannel.IsNone() || service.subchannel.pl.empty())
			continue;

		int subchid = service.audio_service.subchid;
		if(std::find_if(entries.begin(), entries.end(), [&](const Entry& e) {return e.subchid == subchid;}) != entries.end())
			continue;

		Entry entry;
		entry.subchid = subchid;
		entry.sid = service.sid;
		entry.scids = service.scids;
		entry.start = service.subchannel.start;
		entry.size = service.subchannel.size;
		entries.push_back(entry);
	}

	queue_draw();
}

void DABlinGTKCUBar::SetSelectedSubchId(int subchid) {
	if(selected_subchid == subchid)
		return;

	selected_subchid = subchid;
	queue_draw();
}

bool DABlinGTKCUBar::on_draw(const Cairo::RefPtr<Cairo::Context>& cr) {
	Gtk::Allocation allocation = get_allocation();
	const double width = allocation.get_width();
	const double height = allocation.get_height();

	// background represents unused CU capacity
	cr->set_source_rgb(0.88, 0.88, 0.88);
	cr->rectangle(0, 0, width, height);
	cr->fill();

	for(const Entry& entry : entries) {
		double x0 = width * entry.start / total_cus;
		double x1 = width * (entry.start + entry.size) / total_cus;
		if(x1 > width)
			x1 = width;
		if(x0 >= x1)
			continue;

		bool selected = entry.subchid == selected_subchid;

		if(selected)
			cr->set_source_rgb(0.95, 0.62, 0.15);
		else
			cr->set_source_rgb(0.55, 0.72, 0.88);
		cr->rectangle(x0, 1, x1 - x0, height - 2);
		cr->fill_preserve();

		cr->set_source_rgb(0.3, 0.3, 0.3);
		cr->set_line_width(1);
		cr->stroke();

		// SubChId/SID (smaller font) above the CU count, clipped to the block so nothing bleeds into the next one
		cr->save();
		cr->rectangle(x0, 0, x1 - x0, height);
		cr->clip();

		Pango::FontDescription small_font = get_pango_context()->get_font_description();
		small_font.set_size(small_font.get_size() * 0.7);

		Glib::RefPtr<Pango::Layout> header_layout = create_pango_layout(
				std::to_string(entry.subchid) + " · " + StringTools::IntToHex(entry.sid, 4).substr(2));
		header_layout->set_font_description(small_font);
		int header_width, header_height;
		header_layout->get_pixel_size(header_width, header_height);

		Glib::RefPtr<Pango::Layout> count_layout = create_pango_layout(std::to_string(entry.size));
		int count_width, count_height;
		count_layout->get_pixel_size(count_width, count_height);

		double content_height = header_height + count_height;
		double y = (height - content_height) / 2;

		cr->set_source_rgb(0.1, 0.1, 0.1);
		cr->move_to(x0 + (x1 - x0 - header_width) / 2, y);
		header_layout->show_in_cairo_context(cr);

		cr->move_to(x0 + (x1 - x0 - count_width) / 2, y + header_height);
		count_layout->show_in_cairo_context(cr);

		cr->restore();
	}

	return true;
}

bool DABlinGTKCUBar::on_button_press_event(GdkEventButton* event) {
	if(!is_sensitive() || event->type != GDK_BUTTON_PRESS || event->button != 1)
		return false;

	Gtk::Allocation allocation = get_allocation();
	const double width = allocation.get_width();

	for(const Entry& entry : entries) {
		double x0 = width * entry.start / total_cus;
		double x1 = width * (entry.start + entry.size) / total_cus;
		if(x1 > width)
			x1 = width;

		if(event->x >= x0 && event->x < x1) {
			signal_service_clicked.emit(entry.sid, entry.scids);
			break;
		}
	}

	return true;
}
