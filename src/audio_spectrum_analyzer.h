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

#ifndef AUDIO_SPECTRUM_ANALYZER_H_
#define AUDIO_SPECTRUM_ANALYZER_H_

#include <cstddef>
#include <cstdint>
#include <vector>


// --- AudioSpectrumAnalyzer -----------------------------------------------------------------
// turns interleaved signed 16bit PCM audio into a per-channel magnitude spectrum (dBFS), via an
// independent Hann-windowed FFT per channel, computed every hop_size samples over a sliding
// window of fft_size samples (75% overlap); the linear FFT bins are grouped into num_bands
// logarithmically spaced bands from min_freq to max_freq, and the instantaneous spectrum is
// smoothed over time (per band, in the linear magnitude domain) with a ~100ms time constant
class AudioSpectrumAnalyzer {
private:
	static constexpr size_t fft_size = 8192;
	static constexpr size_t hop_size = 2048;
	static constexpr double smoothing_time_constant_s = 0.1;	// ~100ms
	static constexpr double silence_floor_dbfs = -90.0;

	std::vector<std::vector<double>> ring;		// [channel][fft_size]; newest sample overwrites oldest
	std::vector<size_t> ring_pos;			// next write position per channel
	std::vector<size_t> ring_filled;		// number of valid samples seen so far, capped at fft_size
	size_t samples_since_hop;			// shared across channels, as they advance in lockstep
	std::vector<double> window;			// Hann window, precomputed once

	std::vector<std::vector<double>> band_smoothed;	// [channel][num_bands], linear amplitude
	std::vector<bool> band_smoothed_init;			// per channel: whether band_smoothed has a real value yet

	int cached_samplerate;
	std::vector<size_t> band_bin_lo, band_bin_hi;	// [num_bands]; FFT bin range covered by each band, for cached_samplerate
	void UpdateBandRanges(int samplerate);
public:
	static constexpr size_t num_bands = 192;
	static constexpr double min_freq = 20.0;
	static constexpr double max_freq = 20000.0;

	AudioSpectrumAnalyzer();

	// feeds interleaved samples (num_samples total, across all channels); returns one magnitude
	// spectrum (dBFS, num_bands log-spaced bands from min_freq to max_freq) per channel once a hop
	// boundary was crossed by this call, or an empty vector per channel otherwise
	std::vector<std::vector<double>> Process(const int16_t* samples, size_t num_samples, int channels, int samplerate);

	void Reset();
};

#endif /* AUDIO_SPECTRUM_ANALYZER_H_ */
