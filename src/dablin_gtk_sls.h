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
	Gtk::Image image;
	Gtk::LinkButton link_button;
	Gtk::ProgressBar progress_file;

	Glib::RefPtr<Gdk::Pixbuf> pixbuf_waiting;
public:
	static const int max_slide_width = 320;
	static const int max_slide_height = 240;

	DABlinGTKSlideshowContent();

	void AwaitSlide();
	void UpdateSlide(const MOT_FILE& slide);
	void UpdateFileProgress(const double fraction);
	void ClearSlide() {image.clear();}
	bool IsEmptySlide() {return image.get_storage_type() == Gtk::ImageType::IMAGE_EMPTY;}
};


#endif /* DABLIN_GTK_SLS_H_ */
