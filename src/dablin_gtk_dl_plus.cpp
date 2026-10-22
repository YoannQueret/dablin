/*
    DABlin - capital DAB experience
    Copyright (C) 2021-2022 Stefan Pöschel

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

#include "dablin_gtk_dl_plus.h"



// --- DABlinGTKDLPlusContent -----------------------------------------------------------------
DABlinGTKDLPlusContent::DABlinGTKDLPlusContent() {
	list_dl_plus_liststore = Gtk::ListStore::create(list_dl_plus_cols);
	list_dl_plus_liststore->set_sort_column(list_dl_plus_cols.col_content_type, Gtk::SORT_ASCENDING);

	list_dl_plus.set_model(list_dl_plus_liststore);
	list_dl_plus.set_hexpand(true);
	list_dl_plus.insert_column_with_data_func(-1, "Content type", cell_renderer_text, sigc::mem_fun(*this, &DABlinGTKDLPlusContent::RenderContentType));
	list_dl_plus.insert_column_with_data_func(-1, "Text", cell_renderer_text, sigc::mem_fun(*this, &DABlinGTKDLPlusContent::RenderText));

	set_hexpand(true);
	attach(list_dl_plus, 0, 0, 1, 1);
}

void DABlinGTKDLPlusContent::RenderContentType(Gtk::CellRenderer* cell, const Gtk::TreeModel::iterator& iter) {
	Gtk::CellRendererText *rt = (Gtk::CellRendererText*) cell;

	int content_type = (*iter)[list_dl_plus_cols.col_content_type];
	bool item_running = (*iter)[list_dl_plus_cols.col_item_running];

	rt->property_text() = DynamicLabelDecoder::ConvertDLPlusContentTypeToString(content_type);
	rt->property_family() = "Monospace";
	rt->property_foreground_rgba() = Gdk::RGBA("black");
	rt->property_background_rgba() = (content_type < 12 && item_running) ? Gdk::RGBA("light green") : Gdk::RGBA("grey90");	// highlight running ITEM objects
	rt->property_weight() = Pango::WEIGHT_NORMAL;
}

void DABlinGTKDLPlusContent::RenderText(Gtk::CellRenderer* cell, const Gtk::TreeModel::iterator& iter) {
	Gtk::CellRendererText *rt = (Gtk::CellRendererText*) cell;

	int content_type = (*iter)[list_dl_plus_cols.col_content_type];
	std::string text = (*iter)[list_dl_plus_cols.col_text];
	bool deleted = (*iter)[list_dl_plus_cols.col_deleted];

	// color highlighting
	std::string color = "black";
	if(content_type == 1)	// ITEM.TITLE
		color = "blue";
	if(content_type >= 12 && content_type < 31)	// INFO
		color = "green";
	if(content_type >= 41 && content_type < 54)	// INTERACTIVITY
		color = "blue";

	rt->property_text() = text;
	rt->property_family() = "";
	rt->property_foreground_rgba() = Gdk::RGBA(color);
	rt->property_background_rgba() = Gdk::RGBA(deleted ? "light grey" : "white");	// indicate deleted objects
	rt->property_weight() = content_type < 12 ? Pango::WEIGHT_BOLD : Pango::WEIGHT_NORMAL;	// highlight ITEM objects
}

void DABlinGTKDLPlusContent::UpdateDLPlusInfo(const DL_STATE& dl) {
	// add/update DL Plus objects
	Gtk::ListStore::Children children = list_dl_plus_liststore->children();

	// process objects
	for(const DL_PLUS_OBJECT& obj : dl.dl_plus_objects) {
		// ignore DUMMY tags
		if(obj.content_type == 0)
			continue;

		bool deleted = obj.text == " ";

		// get row (add new one, if needed and no deletion)
		Gtk::ListStore::iterator row_it = std::find_if(
				children.begin(), children.end(),
				[&](const Gtk::TreeRow& row)->bool {
					return row[list_dl_plus_cols.col_content_type] == obj.content_type;
				}
		);
		bool add_new_row = row_it == children.end();
		if(add_new_row && !deleted)
			row_it = list_dl_plus_liststore->append();

		// continue, if no existing object for received deletion
		if(row_it == children.end())
			continue;

		Gtk::TreeRow row = *row_it;
		if(add_new_row)
			row[list_dl_plus_cols.col_content_type] = obj.content_type;
		if(!deleted)
			row[list_dl_plus_cols.col_text] = obj.text;
		row[list_dl_plus_cols.col_deleted] = deleted;
	}

	// forward item running flag to all existing objects
	for(const Gtk::TreeRow& row : children)
		row[list_dl_plus_cols.col_item_running] = dl.dl_plus_ir;

	set_tooltip_text(
			"Item toggle bit: " + std::to_string(dl.dl_plus_it ? 1 : 0) + "\n"
			"Item running bit: " + std::to_string(dl.dl_plus_ir ? 1 : 0)
	);
}

void DABlinGTKDLPlusContent::ClearDLPlusInfo() {
	list_dl_plus_liststore->clear();
	list_dl_plus.columns_autosize();
	list_dl_plus.check_resize();	// prevent resizing bug
	set_tooltip_text("");
}
