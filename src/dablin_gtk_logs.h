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

#ifndef DABLIN_GTK_LOGS_H_
#define DABLIN_GTK_LOGS_H_

#include <string>

#include <gtkmm.h>



// --- DABlinGTKLogColumns -----------------------------------------------------------------
class DABlinGTKLogColumns : public Gtk::TreeModelColumnRecord {
public:
	Gtk::TreeModelColumn<Glib::ustring> col_message;

	DABlinGTKLogColumns() {
		add(col_message);
	}
};


// --- DABlinGTKLogsWindow -----------------------------------------------------------------
// floating window listing all hot changes detected on the ensemble (most recent on top)
class DABlinGTKLogsWindow : public Gtk::Window {
private:
	Gtk::ScrolledWindow logs_scroll;
	Gtk::TreeView logs_table;
	DABlinGTKLogColumns logs_cols;
	Glib::RefPtr<Gtk::ListStore> logs_liststore;

	int prev_parent_x;
	int prev_parent_y;
public:
	DABlinGTKLogsWindow();

	void AlignToParent();

	void AppendLogMessage(const std::string& message);
	void Clear() {logs_liststore->clear();}
};


#endif /* DABLIN_GTK_LOGS_H_ */
