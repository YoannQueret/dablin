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

#ifndef DABLIN_GTK_LEVEL_METER_H_
#define DABLIN_GTK_LEVEL_METER_H_

#include <chrono>
#include <vector>

#include <gtkmm.h>


// --- DABlinGTKLevelMeter -----------------------------------------------------------------
// vertical dBFS peak level meter for the currently playing service's audio;
// one bar per channel (mono: 1 bar, stereo: 2 bars, left first), with a dB scale
// and a peak-hold marker for the highest level reached in the last 2 seconds
class DABlinGTKLevelMeter : public Gtk::DrawingArea {
private:
	std::vector<double> levels_dbfs;
	std::vector<double> peak_dbfs;
	std::vector<std::chrono::steady_clock::time_point> peak_time;

	bool on_draw(const Cairo::RefPtr<Cairo::Context>& cr) override;
	void DrawBar(const Cairo::RefPtr<Cairo::Context>& cr, double x, double bar_width, double height, double dbfs, double peak);

	// maps a dBFS value to a (non-linear) fraction of the scale's height, 0 = min_dbfs (bottom), 1 = max_dbfs (top);
	// compresses the low end so there is more resolution close to 0dBFS, where level differences matter most
	static double LevelToFraction(double dbfs);
public:
	static constexpr double min_dbfs = -60.0;
	static constexpr double max_dbfs = 0.0;

	DABlinGTKLevelMeter();

	void SetChannels(size_t channels);
	void SetLevels(const std::vector<double>& dbfs);
	void Reset();
};

#endif /* DABLIN_GTK_LEVEL_METER_H_ */
