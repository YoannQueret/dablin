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
#include <set>


// --- DABlinGTKCUBar -----------------------------------------------------------------
DABlinGTKCUBar::DABlinGTKCUBar() : selected_subchid(-1) {
	set_size_request(-1, 44);
	set_hexpand(true);

	set_has_tooltip(true);
	signal_query_tooltip().connect(sigc::mem_fun(*this, &DABlinGTKCUBar::on_query_tooltip));

	add_events(Gdk::BUTTON_PRESS_MASK);
}

const DABlinGTKCUBar::Entry* DABlinGTKCUBar::GetEntryAt(double x) const {
	Gtk::Allocation allocation = get_allocation();
	const double width = allocation.get_width();

	for(const Entry& entry : entries) {
		double x0 = width * entry.start / total_cus;
		double x1 = width * (entry.start + entry.size) / total_cus;
		if(x1 > width)
			x1 = width;

		if(x >= x0 && x < x1)
			return &entry;
	}
	return nullptr;
}

bool DABlinGTKCUBar::on_query_tooltip(int x, int /*y*/, bool keyboard_tooltip, const Glib::RefPtr<Gtk::Tooltip>& tooltip) {
	if(keyboard_tooltip)
		return false;

	const Entry* entry = GetEntryAt(x);
	if(!entry)
		return false;

	std::string text = "SubChId: " + std::to_string(entry->subchid);
	if(entry->has_sid)
		text += "\n" "SID: " + StringTools::SidToHex(entry->sid);
	text += "\n" "CUs: " + std::to_string(entry->start) + "-" + std::to_string(entry->start + entry->size) +
			" (" + std::to_string(entry->size) + ")";

	tooltip->set_text(text);
	return true;
}

void DABlinGTKCUBar::SetServices(const std::vector<LISTED_SERVICE>& services) {
	this->services = services;
	RebuildEntries();
}

void DABlinGTKCUBar::SetSubchannels(const std::map<int, FIC_SUBCHANNEL>& subchannels) {
	this->subchannels = subchannels;
	RebuildEntries();
}

void DABlinGTKCUBar::RebuildEntries() {
	entries.clear();

	// one block per distinct sub-channel: a service's secondary components share the
	// same sub-channel (and thus the same CUs) as its primary component. Data-only rows
	// (is_audio false) aren't turned into blocks here - they're already covered generically
	// by the subchannels loop below - but their SId (if known) is collected for it to use.
	std::set<int> handled_subchids;
	std::map<int,int> data_sids;
	for(const LISTED_SERVICE& service : services) {
		if(!service.is_audio) {
			if(service.sid != LISTED_SERVICE::sid_none)
				data_sids[service.subchid] = service.sid;
			continue;
		}
		if(service.subchannel.IsNone() || service.subchannel.pl.empty())
			continue;

		int subchid = service.subchid;
		if(!handled_subchids.insert(subchid).second)
			continue;

		Entry entry;
		entry.subchid = subchid;
		entry.sid = service.sid;
		entry.has_sid = true;
		entry.scids = service.scids;
		entry.start = service.subchannel.start;
		entry.size = service.subchannel.size;
		entry.is_audio = true;
		entries.push_back(entry);
	}

	// any remaining sub-channel (most commonly a data service, e.g. EPG) still occupies
	// real CU capacity even though dablin can't play it - shown as a distinct block
	// (with its SId, if known), so the bar's background doesn't misleadingly look free there
	for(const std::pair<const int, FIC_SUBCHANNEL>& sc : subchannels) {
		if(sc.second.IsNone() || sc.second.pl.empty() || handled_subchids.find(sc.first) != handled_subchids.end())
			continue;

		std::map<int,int>::const_iterator sid_it = data_sids.find(sc.first);

		Entry entry;
		entry.subchid = sc.first;
		entry.has_sid = sid_it != data_sids.end();
		entry.sid = entry.has_sid ? sid_it->second : 0;
		entry.scids = LISTED_SERVICE::scids_none;
		entry.start = sc.second.start;
		entry.size = sc.second.size;
		entry.is_audio = false;
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

		bool selected = entry.is_audio && entry.subchid == selected_subchid;

		if(selected)
			cr->set_source_rgb(0.95, 0.62, 0.15);
		else if(entry.is_audio)
			cr->set_source_rgb(0.55, 0.72, 0.88);
		else
			cr->set_source_rgb(0.70, 0.70, 0.70);	// data service (e.g. EPG) - not playable
		cr->rectangle(x0, 1, x1 - x0, height - 2);
		cr->fill_preserve();

		cr->set_source_rgb(0.3, 0.3, 0.3);
		cr->set_line_width(1);
		cr->stroke();

		// SubChId[/SID] (smaller font) above the CU count, clipped to the block so nothing bleeds into the next one
		cr->save();
		cr->rectangle(x0, 0, x1 - x0, height);
		cr->clip();

		Pango::FontDescription small_font = get_pango_context()->get_font_description();
		small_font.set_size(small_font.get_size() * 0.7);

		Glib::RefPtr<Pango::Layout> header_layout = create_pango_layout(entry.has_sid ?
				std::to_string(entry.subchid) + " · " + StringTools::SidToHex(entry.sid) :
				std::to_string(entry.subchid));
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

	const Entry* entry = GetEntryAt(event->x);
	if(entry && entry->is_audio)
		signal_service_clicked.emit(entry->sid, entry->scids);

	return true;
}
