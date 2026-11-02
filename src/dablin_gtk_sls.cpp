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

#include "dablin_gtk_sls.h"

#include <fstream>



// --- DABlinGTKSlideshowContent -----------------------------------------------------------------
DABlinGTKSlideshowContent::DABlinGTKSlideshowContent() {
	pixbuf_waiting = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, false, 8, max_slide_width, max_slide_height);
	pixbuf_waiting->fill(0x003000FF);

	// a slide is at most 320x240 pixels; reserve exactly that so the box neither grows nor shrinks with slides
	image.set_size_request(max_slide_width, max_slide_height);
	image.set_margin_start(5);
	image.set_margin_end(5);
	image.set_margin_bottom(5);

	// GtkImage has no window of its own, so wrap it to reliably receive the right-click
	image_event_box.add(image);
	image_event_box.add_events(Gdk::BUTTON_PRESS_MASK);
	image_event_box.signal_button_press_event().connect(sigc::mem_fun(*this, &DABlinGTKSlideshowContent::on_image_button_press_event));

	save_slide_menu_item.set_label("Save slide as...");
	save_slide_menu_item.signal_activate().connect(sigc::mem_fun(*this, &DABlinGTKSlideshowContent::on_save_slide_clicked));
	slide_context_menu.append(save_slide_menu_item);

	copy_slide_menu_item.set_label("Copy");
	copy_slide_menu_item.signal_activate().connect(sigc::mem_fun(*this, &DABlinGTKSlideshowContent::on_copy_slide_clicked));
	slide_context_menu.append(copy_slide_menu_item);

	slide_context_menu.show_all();

	attach(image_event_box, 0, 0, 1, 1);
	attach_next_to(link_button, image_event_box, Gtk::POS_BOTTOM, 1, 1);
	attach_next_to(progress_file, link_button, Gtk::POS_BOTTOM, 1, 1);
}

void DABlinGTKSlideshowContent::AwaitSlide() {
	image.set(pixbuf_waiting);
	image.set_tooltip_text("Waiting for slide...");

	link_button.hide();
	progress_file.hide();
}

void DABlinGTKSlideshowContent::UpdateSlide(const MOT_FILE& slide) {
	std::string type_mime = "";
	std::string type_display = "unknown";

	switch(slide.content_sub_type) {
	case MOT_FILE::CONTENT_SUB_TYPE_JFIF:
		type_mime = "image/jpeg";
		type_display = "JPEG";
		break;
	case MOT_FILE::CONTENT_SUB_TYPE_PNG:
		type_mime = "image/png";
		type_display = "PNG";
		break;
	}

	Glib::RefPtr<Gdk::PixbufLoader> pixbuf_loader = Gdk::PixbufLoader::create(type_mime, true);
	pixbuf_loader->write(&slide.data[0], slide.data.size());
	pixbuf_loader->close();

	Glib::RefPtr<Gdk::Pixbuf> pixbuf = pixbuf_loader->get_pixbuf();
	if(!pixbuf)
		return;

	// keep the raw, still encoded bytes around, so a right-click can offer saving the exact original file
	current_slide = slide;

	// update image
	image.set(pixbuf);
	image.set_tooltip_text(
			"Resolution: " + std::to_string(pixbuf->get_width()) + "x" + std::to_string(pixbuf->get_height()) + " pixels\n"
			"Size: " + std::to_string(slide.data.size()) + " bytes\n"
			"Format: " + type_display + "\n"
			"Content name: \"" + slide.content_name + "\"\n"
			"Content name charset: " + slide.content_name_charset);

	// update ClickThroughURL link
	if(!slide.click_through_url.empty()) {
		link_button.set_label(slide.click_through_url);
		link_button.set_tooltip_text(slide.click_through_url);
		link_button.set_uri(slide.click_through_url);

		// ensure that the label (created/replaced by set_label) does not extend the slide
		Gtk::Label* l = (Gtk::Label*) link_button.get_child();
		l->set_max_width_chars(1);
		l->set_ellipsize(Pango::ELLIPSIZE_END);

		link_button.show();
	} else {
		link_button.hide();
	}

	// hide file progress
	progress_file.hide();
}

void DABlinGTKSlideshowContent::UpdateFileProgress(const double fraction) {
	// update/show file progress
	if(fraction == -1)
		progress_file.pulse();	// unknown progress
	else
		progress_file.set_fraction(fraction);
	progress_file.show();
}

bool DABlinGTKSlideshowContent::on_image_button_press_event(GdkEventButton* button_event) {
	if(button_event->type != GDK_BUTTON_PRESS || button_event->button != 3)
		return false;
	if(current_slide.data.empty())
		return false;

	slide_context_menu.popup_at_pointer((GdkEvent*) button_event);
	return true;
}

void DABlinGTKSlideshowContent::on_save_slide_clicked() {
	if(current_slide.data.empty())
		return;

	std::string extension;
	switch(current_slide.content_sub_type) {
	case MOT_FILE::CONTENT_SUB_TYPE_JFIF:
		extension = ".jpg";
		break;
	case MOT_FILE::CONTENT_SUB_TYPE_PNG:
		extension = ".png";
		break;
	default:
		extension = "";
		break;
	}

	std::string suggested_name = !current_slide.content_name.empty() ? current_slide.content_name : "slide" + extension;

	Gtk::Window* parent = (Gtk::Window*) get_toplevel();
	Gtk::FileChooserDialog dialog(*parent, "Save slide as...", Gtk::FILE_CHOOSER_ACTION_SAVE);
	dialog.set_do_overwrite_confirmation(true);
	dialog.set_current_name(suggested_name);
	dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
	dialog.add_button("_Save", Gtk::RESPONSE_OK);

	if(dialog.run() != Gtk::RESPONSE_OK)
		return;

	std::string filename = dialog.get_filename();

	std::ofstream file(filename, std::ios::binary);
	file.write((const char*) current_slide.data.data(), current_slide.data.size());
}

void DABlinGTKSlideshowContent::on_copy_slide_clicked() {
	Glib::RefPtr<Gdk::Pixbuf> pixbuf = image.get_pixbuf();
	if(!pixbuf)
		return;

	Gtk::Clipboard::get()->set_image(pixbuf);
}
