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

#include "audio_spectrum_analyzer.h"

#include "fft.h"

#include <algorithm>
#include <cmath>
#include <complex>

// out-of-class definitions, as odr-used in C++11
constexpr size_t AudioSpectrumAnalyzer::fft_size;
constexpr size_t AudioSpectrumAnalyzer::hop_size;
constexpr double AudioSpectrumAnalyzer::smoothing_time_constant_s;
constexpr double AudioSpectrumAnalyzer::silence_floor_dbfs;
constexpr size_t AudioSpectrumAnalyzer::num_bands;
constexpr double AudioSpectrumAnalyzer::min_freq;
constexpr double AudioSpectrumAnalyzer::max_freq;

AudioSpectrumAnalyzer::AudioSpectrumAnalyzer() {
	samples_since_hop = 0;
	cached_samplerate = 0;

	window.resize(fft_size);
	for(size_t n = 0; n < fft_size; n++)
		window[n] = 0.5 - 0.5 * std::cos(2 * M_PI * n / (fft_size - 1));
}

// precomputes, for the given samplerate, which range of FFT bins falls into each of the
// logarithmically spaced display bands; bands narrower than one bin (always the case at the low
// end, where human hearing's own frequency resolution is just as coarse) fall back to that single
// nearest bin, rather than leaving a gap
void AudioSpectrumAnalyzer::UpdateBandRanges(int samplerate) {
	cached_samplerate = samplerate;
	band_bin_lo.assign(num_bands, 0);
	band_bin_hi.assign(num_bands, 0);

	double bin_hz = (double) samplerate / fft_size;
	size_t num_bins = fft_size / 2;

	for(size_t i = 0; i < num_bands; i++) {
		double freq_lo = min_freq * std::pow(max_freq / min_freq, (double) i / num_bands);
		double freq_hi = min_freq * std::pow(max_freq / min_freq, (double) (i + 1) / num_bands);

		size_t bin_lo = (size_t) std::min((double) (num_bins - 1), std::round(freq_lo / bin_hz));
		size_t bin_hi = (size_t) std::min((double) num_bins, std::round(freq_hi / bin_hz));
		if(bin_hi <= bin_lo)
			bin_hi = bin_lo + 1;	// always cover at least the one nearest bin

		band_bin_lo[i] = bin_lo;
		band_bin_hi[i] = bin_hi;
	}
}

std::vector<std::vector<double>> AudioSpectrumAnalyzer::Process(const int16_t* samples, size_t num_samples, int channels, int samplerate) {
	channels = std::max(channels, 1);
	if((size_t) channels != ring.size()) {
		ring.assign(channels, std::vector<double>(fft_size, 0.0));
		ring_pos.assign(channels, 0);
		ring_filled.assign(channels, 0);
		band_smoothed.assign(channels, std::vector<double>(num_bands, 0.0));
		band_smoothed_init.assign(channels, false);
		samples_since_hop = 0;
	}

	if(samplerate != cached_samplerate)
		UpdateBandRanges(samplerate);

	size_t num_frames = num_samples / channels;
	for(size_t i = 0; i < num_frames; i++) {
		for(int ch = 0; ch < channels; ch++) {
			ring[ch][ring_pos[ch]] = samples[i * channels + ch];
			ring_pos[ch] = (ring_pos[ch] + 1) % fft_size;
			if(ring_filled[ch] < fft_size)
				ring_filled[ch]++;
		}
	}
	samples_since_hop += num_frames;

	std::vector<std::vector<double>> result(channels);

	// a DAB audio frame (24ms) is always well under hop_size samples for every DAB samplerate, so
	// a single call can cross at most one hop boundary
	if(samplerate <= 0 || samples_since_hop < hop_size)
		return result;
	samples_since_hop -= hop_size;

	// recovers the amplitude of a full-scale sine wave at ~0dBFS, independent of FFT size: a
	// coherent sine's FFT magnitude is N/2 times its amplitude times the window's coherent gain
	// (0.5 for a Hann window)
	static const double norm = 2.0 / (fft_size * 0.5);
	double dt = (double) hop_size / samplerate;
	double alpha = 1.0 - std::exp(-dt / smoothing_time_constant_s);

	for(int ch = 0; ch < channels; ch++) {
		if(ring_filled[ch] < fft_size)
			continue;

		std::vector<std::complex<double>> buf(fft_size);
		for(size_t n = 0; n < fft_size; n++) {
			size_t idx = (ring_pos[ch] + n) % fft_size;	// chronological order, oldest sample first
			buf[n] = ring[ch][idx] * window[n];
		}

		FFT::Transform(buf);

		std::vector<double>& smoothed = band_smoothed[ch];
		std::vector<double> bands_db(num_bands);
		for(size_t b = 0; b < num_bands; b++) {
			size_t lo = band_bin_lo[b], hi = band_bin_hi[b];

			// the band's level is its loudest bin, not an average or a power sum: both of those
			// would dilute a narrowband tone across its band's (often much wider, at low
			// frequencies) bin range, reading well below the tone's actual, calibrated peak level
			double peak_mag = 0.0;
			for(size_t k = lo; k < hi; k++)
				peak_mag = std::max(peak_mag, std::abs(buf[k]));
			double amplitude = peak_mag * norm;

			// temporal smoothing of the instantaneous spectrum, in the linear magnitude domain
			if(!band_smoothed_init[ch])
				smoothed[b] = amplitude;
			else
				smoothed[b] += alpha * (amplitude - smoothed[b]);

			double dbfs = smoothed[b] > 0 ? 20.0 * std::log10(smoothed[b] / 32768.0) : silence_floor_dbfs;
			bands_db[b] = std::max(dbfs, silence_floor_dbfs);
		}
		band_smoothed_init[ch] = true;

		result[ch] = std::move(bands_db);
	}

	return result;
}

void AudioSpectrumAnalyzer::Reset() {
	ring.clear();
	ring_pos.clear();
	ring_filled.clear();
	band_smoothed.clear();
	band_smoothed_init.clear();
	samples_since_hop = 0;
	cached_samplerate = 0;
}
