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

#include "dablin_gtk_level_meter.h"

#include <algorithm>
#include <cmath>

static const int SCALE_WIDTH = 26;
static const int BAR_WIDTH = 16;
static const int BAR_GAP = 3;
static const int MAX_CHANNELS = 2;	// DAB audio is mono or stereo at most
static const int BARS_AREA_WIDTH = MAX_CHANNELS * BAR_WIDTH + (MAX_CHANNELS - 1) * BAR_GAP;
static const std::chrono::seconds PEAK_HOLD_DURATION{2};


// --- DABlinGTKLevelMeter -----------------------------------------------------------------
// out-of-class definitions, as odr-used (e.g. passed by reference into std::min/std::max) in C++11
constexpr double DABlinGTKLevelMeter::min_dbfs;
constexpr double DABlinGTKLevelMeter::max_dbfs;

DABlinGTKLevelMeter::DABlinGTKLevelMeter() {
	// fixed width regardless of channel count, so mono/stereo switches never resize the widget:
	// in mono, the single bar simply spans the full area the two stereo bars would occupy
	set_size_request(SCALE_WIDTH + BARS_AREA_WIDTH + 4, -1);
	set_vexpand(true);
	set_tooltip_text("Audio level (dBFS)");
	SetChannels(1);
}

void DABlinGTKLevelMeter::SetChannels(size_t channels) {
	channels = std::max((size_t) 1, channels);
	if(channels == levels_dbfs.size())
		return;

	levels_dbfs.assign(channels, min_dbfs);
	peak_dbfs.assign(channels, min_dbfs);
	peak_time.assign(channels, std::chrono::steady_clock::now());
	queue_draw();
}

void DABlinGTKLevelMeter::SetLevels(const std::vector<double>& dbfs) {
	if(dbfs.size() != levels_dbfs.size())
		SetChannels(dbfs.size());

	auto now = std::chrono::steady_clock::now();
	bool changed = false;

	for(size_t i = 0; i < levels_dbfs.size() && i < dbfs.size(); i++) {
		double clamped = std::min(std::max(dbfs[i], min_dbfs), max_dbfs);
		if(clamped != levels_dbfs[i]) {
			levels_dbfs[i] = clamped;
			changed = true;
		}

		// new peak, or the held one is older than the hold duration: (re)start the hold from here
		if(clamped >= peak_dbfs[i] || now - peak_time[i] > PEAK_HOLD_DURATION) {
			if(clamped != peak_dbfs[i])
				changed = true;
			peak_dbfs[i] = clamped;
			peak_time[i] = now;
		}
	}

	if(changed)
		queue_draw();
}

void DABlinGTKLevelMeter::Reset() {
	auto now = std::chrono::steady_clock::now();
	std::fill(levels_dbfs.begin(), levels_dbfs.end(), min_dbfs);
	std::fill(peak_dbfs.begin(), peak_dbfs.end(), min_dbfs);
	std::fill(peak_time.begin(), peak_time.end(), now);
	queue_draw();
}

double DABlinGTKLevelMeter::LevelToFraction(double dbfs) {
	double t = (max_dbfs - dbfs) / (max_dbfs - min_dbfs);	// 0 at 0dBFS (top), 1 at the floor (bottom), linear
	double t_nonlinear = std::sqrt(std::min(std::max(t, 0.0), 1.0));	// square-root scale: expands the top, compresses the bottom
	return 1.0 - t_nonlinear;	// back to "fraction from the bottom"
}

void DABlinGTKLevelMeter::DrawBar(const Cairo::RefPtr<Cairo::Context>& cr, double x, double bar_width, double height, double dbfs, double peak) {
	auto px_from_bottom = [&](double d) {return height * LevelToFraction(d);};

	// background represents the unfilled part of the bar
	cr->set_source_rgb(0.15, 0.15, 0.15);
	cr->rectangle(x, 0, bar_width, height);
	cr->fill();

	// clip to the currently filled portion of the bar (from the bottom up to the current level)
	cr->save();
	cr->rectangle(x, height - px_from_bottom(dbfs), bar_width, px_from_bottom(dbfs));
	cr->clip();

	// color zones (broadcast VU meter convention): green/yellow/red, drawn bottom-up
	cr->set_source_rgb(0.25, 0.75, 0.25);
	cr->rectangle(x, height - px_from_bottom(-12.0), bar_width, px_from_bottom(-12.0));
	cr->fill();

	cr->set_source_rgb(0.9, 0.8, 0.15);
	cr->rectangle(x, height - px_from_bottom(-3.0), bar_width, px_from_bottom(-3.0) - px_from_bottom(-12.0));
	cr->fill();

	cr->set_source_rgb(0.85, 0.2, 0.2);
	cr->rectangle(x, 0, bar_width, height - px_from_bottom(-3.0));
	cr->fill();

	cr->restore();

	// peak-hold marker: highest level reached in the last 2 seconds
	double peak_y = height - height * LevelToFraction(peak);
	cr->set_source_rgb(1.0, 1.0, 1.0);
	cr->rectangle(x, std::max(peak_y - 1, 0.0), bar_width, 2);
	cr->fill();

	// border
	cr->set_source_rgb(0.3, 0.3, 0.3);
	cr->set_line_width(1);
	cr->rectangle(x + 0.5, 0.5, bar_width - 1, height - 1);
	cr->stroke();
}

bool DABlinGTKLevelMeter::on_draw(const Cairo::RefPtr<Cairo::Context>& cr) {
	Gtk::Allocation allocation = get_allocation();
	const double height = allocation.get_height();

	auto y_from_top = [&](double d) {return height - height * LevelToFraction(d);};

	// dB scale, so it is clear whether the bottom is -40, -60, ... (closer spacing near 0dBFS reflects the non-linear bar scale)
	static const double ticks[] = {0, -3, -6, -12, -20, -40, -60};

	Pango::FontDescription small_font = get_pango_context()->get_font_description();
	small_font.set_size(small_font.get_size() * 0.65);

	cr->set_source_rgb(0.4, 0.4, 0.4);
	cr->set_line_width(1);
	for(double tick : ticks) {
		if(tick < min_dbfs || tick > max_dbfs)
			continue;

		double y = y_from_top(tick);
		cr->move_to(SCALE_WIDTH - 4, y);
		cr->line_to(SCALE_WIDTH, y);
		cr->stroke();

		Glib::RefPtr<Pango::Layout> label = create_pango_layout(std::to_string((int) tick));
		label->set_font_description(small_font);
		int label_width, label_height;
		label->get_pixel_size(label_width, label_height);

		cr->set_source_rgb(0.4, 0.4, 0.4);
		cr->move_to(SCALE_WIDTH - 6 - label_width, std::min(std::max(y - label_height / 2.0, 0.0), height - label_height));
		label->show_in_cairo_context(cr);
	}

	// one bar per channel (left first), to the right of the scale; whatever the channel count,
	// the bars always fill the same total width (mono: one wide bar instead of narrower one)
	size_t channels = levels_dbfs.size();
	double bar_width = (BARS_AREA_WIDTH - (double) (channels - 1) * BAR_GAP) / channels;
	for(size_t ch = 0; ch < channels; ch++) {
		double x = SCALE_WIDTH + ch * (bar_width + BAR_GAP);
		DrawBar(cr, x, bar_width, height, levels_dbfs[ch], peak_dbfs[ch]);
	}

	return true;
}
