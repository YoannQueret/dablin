/*
    DABlin - capital DAB experience
    Copyright (C) 2018-2022 Stefan Pöschel

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

#ifndef DABLIN_GTK_SLS_H_
#define DABLIN_GTK_SLS_H_

#include <string>

#include <gtkmm.h>

#include "mot_manager.h"



// --- DABlinGTKSlideshowContent -----------------------------------------------------------------
// embedded slideshow display; sized to the maximum possible slide dimensions (320x240)
class DABlinGTKSlideshowContent : public Gtk::Grid {
private:
	Gtk::EventBox image_event_box;
	Gtk::Image image;
	Gtk::LinkButton link_button;
	Gtk::ProgressBar progress_file;

	Glib::RefPtr<Gdk::Pixbuf> pixbuf_waiting;

	// the raw (still encoded) bytes of the currently shown slide, if any - kept around
	// purely so a right-click can offer saving the exact, original file to disk
	MOT_FILE current_slide;

	Gtk::Menu slide_context_menu;
	Gtk::MenuItem save_slide_menu_item;
	Gtk::MenuItem copy_slide_menu_item;
	bool on_image_button_press_event(GdkEventButton* button_event);
	void on_save_slide_clicked();
	void on_copy_slide_clicked();
public:
	static const int max_slide_width = 320;
	static const int max_slide_height = 240;

	DABlinGTKSlideshowContent();

	void AwaitSlide();
	void UpdateSlide(const MOT_FILE& slide);
	void UpdateFileProgress(const double fraction);
	void ClearSlide() {image.clear(); current_slide = MOT_FILE();}
	bool IsEmptySlide() {return image.get_storage_type() == Gtk::ImageType::IMAGE_EMPTY;}
};


#endif /* DABLIN_GTK_SLS_H_ */
