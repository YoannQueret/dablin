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

#include "dablin_gtk_logs.h"

#include <time.h>


// --- DABlinGTKLogsWindow -----------------------------------------------------------------
DABlinGTKLogsWindow::DABlinGTKLogsWindow() {
	prev_parent_x = -1;
	prev_parent_y = -1;

	set_title("Logs");
	set_type_hint(Gdk::WINDOW_TYPE_HINT_UTILITY);
	set_default_size(600, 300);
	set_deletable(false);

	logs_liststore = Gtk::ListStore::create(logs_cols);
	logs_table.set_model(logs_liststore);
	logs_table.set_headers_visible(false);
	logs_table.append_column("Log", logs_cols.col_message);

	logs_scroll.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
	logs_scroll.add(logs_table);

	add(logs_scroll);

	show_all_children();
}

void DABlinGTKLogsWindow::AlignToParent() {
	int x, y, w, h, this_x, this_y;
	get_transient_for()->get_position(x, y);
	get_transient_for()->get_size(w, h);
	get_position(this_x, this_y);	// event position doesn't work!

	int curr_parent_x = x + w;
	int curr_parent_y = y;

	// TODO: fix multi head issue
	if(prev_parent_x == -1 && prev_parent_y == -1) {
		// first: align to the right of parent
		move(curr_parent_x + 20, curr_parent_y);	// add some horizontal padding for WM decoration
	} else {
		// later: follow parent
		move(curr_parent_x + (this_x - prev_parent_x), curr_parent_y + (this_y - prev_parent_y));
	}

	prev_parent_x = curr_parent_x;
	prev_parent_y = curr_parent_y;
}

void DABlinGTKLogsWindow::AppendLogMessage(const std::string& message) {
	static const size_t max_log_entries = 1000;

	time_t now = time(nullptr);
	struct tm* now_tm = now != (time_t) -1 ? localtime(&now) : nullptr;
	char ts[10] = "";
	if(now_tm)
		strftime(ts, sizeof(ts), "%H:%M:%S", now_tm);

	// newest entry on top
	Gtk::TreeModel::iterator row_it = logs_liststore->prepend();
	Gtk::TreeModel::Row row = *row_it;
	row[logs_cols.col_message] = std::string(ts) + "  " + message;

	// cap the log size, dropping the oldest entries (now at the end of the list)
	Gtk::TreeModel::Children children = logs_liststore->children();
	while(children.size() > max_log_entries) {
		Gtk::TreeModel::iterator last_it = children.end();
		--last_it;
		logs_liststore->erase(last_it);
	}

	logs_table.scroll_to_row(logs_liststore->get_path(row_it));
}
