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

#ifndef DABLIN_GTK_SPECTRUM_H_
#define DABLIN_GTK_SPECTRUM_H_

#include <chrono>
#include <vector>

#include <gtkmm.h>

#include "ensemble_player.h"


// --- DABlinGTKSpectrum -----------------------------------------------------------------
// frequency spectrum analyzer for the currently playing service's audio: a logarithmic 20Hz-20kHz
// frequency axis, a dB amplitude scale (identical for every channel) and a 2 second peak-hold
// overlaid on the instantaneous spectrum; a right-click context menu switches between two display
// styles sharing that same scale: "bubble" (a semi-transparent filled curve with a thin, bright
// peak-hold curve) and "bar" (coarser gradient-colored bars with a peak-hold cap on each, akin to
// a classic 1/3-octave SPL meter);
// mono shows a single spectrum (floor at the bottom, 0dBFS at the top); stereo shows a mirrored
// display instead, split around a common central axis (floor): the left channel above, the right
// channel below, each growing away from the axis towards its edge as level increases
class DABlinGTKSpectrum : public Gtk::DrawingArea {
private:
	enum class DisplayMode { Bubble, Bar };

	static constexpr double min_dbfs = -90.0;
	static constexpr double max_dbfs = 0.0;

	bool has_data;
	std::vector<std::vector<double>> bands_db;			// [channel][band]
	std::vector<std::vector<double>> peak_db;			// [channel][band]
	std::vector<std::vector<std::chrono::steady_clock::time_point>> peak_time;	// [channel][band]

	DisplayMode display_mode;
	Gtk::Menu context_menu;
	Gtk::RadioMenuItem::Group mode_group;
	Gtk::RadioMenuItem menu_item_bubble, menu_item_bar;
	void SetDisplayMode(DisplayMode mode);

	bool on_draw(const Cairo::RefPtr<Cairo::Context>& cr) override;
	bool on_button_press_event(GdkEventButton* event) override;
	void DrawFrequencyScale(const Cairo::RefPtr<Cairo::Context>& cr, double x, double width, double y);
	void DrawAmplitudeScale(const Cairo::RefPtr<Cairo::Context>& cr, double baseline_y, double height, bool grow_upward);
	void DrawChannel(const Cairo::RefPtr<Cairo::Context>& cr, size_t ch, double x, double width, double baseline_y, double height, bool grow_upward);
	void DrawChannelBar(const Cairo::RefPtr<Cairo::Context>& cr, size_t ch, double x, double width, double baseline_y, double height, bool grow_upward);

	// maps a dBFS value to a fraction of the available height, 0 = min_dbfs (at the baseline), 1 = max_dbfs
	// (at the far edge); compresses the low end, as there is more resolution close to 0dBFS
	static double LevelToFraction(double dbfs);

	// maps a frequency (clamped to the analyzer's [min_freq, max_freq]) to a fraction of the plot
	// width, logarithmically; used only for positioning the frequency scale's tick labels, as the
	// bands themselves already arrive pre-mapped onto that same fixed logarithmic range
	static double FreqToFraction(double freq_hz);
public:
	DABlinGTKSpectrum();

	void SetChannels(size_t channels);
	void SetSpectrum(const AUDIO_SPECTRUM& spectrum);
	void Reset();
};

#endif /* DABLIN_GTK_SPECTRUM_H_ */
