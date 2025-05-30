/*
 * Copyright (C) 2016-2017 Robin Gareus <robin@gareus.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef _dsp_filter_h_
#define _dsp_filter_h_

#include <atomic>
#include <cstdint>
#include <cstring>
#include <assert.h>
#include <glib.h>
#include <glibmm.h>
#include <fftw3.h>

#include "pbd/enum_convert.h"
#include "pbd/malign.h"

#include "ardour/buffer_set.h"
#include "ardour/chan_mapping.h"
#include "ardour/libardour_visibility.h"
#include "ardour/types.h"

namespace ARDOUR { namespace DSP {

	/** C/C++ Shared Memory
	 *
	 * A convenience class representing a C array of float[] or int32_t[]
	 * data values. This is useful for lua scripts to perform DSP operations
	 * directly using C/C++ with CPU Hardware acceleration.
	 *
	 * Access to this memory area is always 4 byte aligned. The data
	 * is interpreted either as float or as int.
	 *
	 * This memory area can also be shared between different instances
	 * or the same lua plugin (DSP, GUI).
	 *
	 * Since memory allocation is not realtime safe it should be
	 * allocated during dsp_init() or dsp_configure().
	 * The memory is free()ed automatically when the lua instance is
	 * destroyed.
	 */
	class DspShm {
		public:
			DspShm (size_t s = 0)
				: _data (0)
				, _size (0)
			{
				assert (sizeof(float) == sizeof (int32_t));
				assert (sizeof(float) == sizeof (int));
				allocate (s);
			}

			~DspShm () {
				cache_aligned_free (_data);
			}

			/** [re] allocate memory in host's memory space
			 *
			 * @param s size, total number of float or integer elements to store.
			 */
			void allocate (size_t s) {
				if (s == _size) { return; }
				cache_aligned_free (_data);
				cache_aligned_malloc ((void**) &_data, sizeof (float) * s);
				if (_data) { _size = s; }
			}

			/** clear memory (set to zero) */
			void clear () {
				memset (_data, 0, sizeof(float) * _size);
			}

			/** access memory as float array
			 *
			 * @param off offset in shared memory region
			 * @returns float[]
			 */
			float* to_float (size_t off) {
				if (off >= _size) { return 0; }
				return &(((float*)_data)[off]);
			}

			/** access memory as integer array
			 *
			 * @param off offset in shared memory region
			 * @returns int_32_t[]
			 */
			int32_t* to_int (size_t off) {
				if (off >= _size) { return 0; }
				return &(((int32_t*)_data)[off]);
			}

			/** atomically set integer at offset
			 *
			 * This involves a memory barrier. This call
			 * is intended for buffers which are
			 * shared with another instance.
			 *
			 * @param off offset in shared memory region
			 * @param val value to set
			 */
			void atomic_set_int (size_t off, int32_t val) {
				/* no read or write that precedes the write we are
				   about to do can be reordered past this fence.
				*/
				std::atomic_thread_fence (std::memory_order_release);
				((int32_t*)_data)[off] = val;
			}

			/** atomically read integer at offset
			 *
			 * This involves a memory barrier. This call
			 * is intended for buffers which are
			 * shared with another instance.
			 *
			 * @param off offset in shared memory region
			 * @returns value at offset
			 */
			int32_t atomic_get_int (size_t off) {
				/* no read that precedes the read we are
				   about to do can be reordered past this fence.
				*/
				std::atomic_thread_fence (std::memory_order_acquire);
				return (((int32_t*)_data)[off]);
			}

		private:
			void* _data;
			size_t _size;
	};

	/** lua wrapper to memset() */
	void memset (float *data, const float val, const uint32_t n_samples);
	/** matrix multiply
	 * multiply every sample of `data' with the corresponding sample at `mult'.
	 *
	 * @param data multiplicand
	 * @param mult multiplicand
	 * @param n_samples number of samples in data and mmult
	 */
	void mmult (float *data, float *mult, const uint32_t n_samples);
	/** calculate peaks
	 *
	 * @param data data to analyze
	 * @param min result, minimum value found in range
	 * @param max result, max value found in range
	 * @param n_samples number of samples to analyze
	 */
	void peaks (const float *data, float &min, float &max, uint32_t n_samples);

	/** non-linear power-scale meter deflection
	 *
	 * @param power signal power (dB)
	 * @returns deflected value
	 */
	float log_meter (float power);
	/** non-linear power-scale meter deflection
	 *
	 * @param coeff signal value
	 * @returns deflected value
	 */
	float log_meter_coeff (float coeff);

	void process_map (BufferSet* bufs,
	                  const ChanCount&   n_out,
	                  const ChanMapping& in_map,
	                  const ChanMapping& out_map,
	                  pframes_t nframes, samplecnt_t offset);

	/** 1st order Low Pass filter */
	class LIBARDOUR_API LowPass {
		public:
			/** instantiate a LPF
			 *
			 * @param samplerate samplerate
			 * @param freq cut-off frequency
			 */
			LowPass (double samplerate, float freq);
			/** process audio data
			 *
			 * @param data pointer to audio-data
			 * @param n_samples number of samples to process
			 */
			void proc (float *data, const uint32_t n_samples);
			/** filter control data
			 *
			 * This is useful for parameter smoothing.
			 *
			 * @param data pointer to control-data array
			 * @param val target value
			 * @param n_samples array length
			 */
			void ctrl (float *data, const float val, const uint32_t n_samples);
			/** update filter cut-off frequency
			 *
			 * @param freq cut-off frequency
			 */
			void set_cutoff (float freq);
			/** reset filter state */
			void reset () { _z =  0.f; }
		private:
			float _rate;
			float _z;
			float _a;
	};

	/** Biquad Filter */
	class LIBARDOUR_API Biquad {
		public:
			enum Type {
				LowPass,
				HighPass,
				BandPassSkirt,
				BandPass0dB,
				Notch,
				AllPass,
				Peaking,
				LowShelf,
				HighShelf,
				MatchedLowPass,
				MatchedHighPass,
				MatchedBandPass0dB,
				MatchedPeaking
			};

			/** Instantiate Biquad Filter
			 *
			 * @param samplerate Samplerate
			 */
			Biquad (double samplerate);
			Biquad (const Biquad &other);

			/** process audio data
			 *
			 * @param data pointer to audio-data
			 * @param n_samples number of samples to process
			 */
			void run (float *data, const uint32_t n_samples);
			/** setup filter, compute coefficients
			 *
			 * @param t filter type (LowPass, HighPass, etc)
			 * @param freq filter frequency
			 * @param Q filter quality
			 * @param gain filter gain
			 */
			void compute (Type t, double freq, double Q, double gain);

			/** setup filter, set coefficients directly */
			void configure (double a1, double a2, double b0, double b1, double b2);

			/* copy coefficients from other instance, retain state */
			void configure (Biquad const&);

			/** filter transfer function (filter response for spectrum visualization)
			 * @param freq frequency
			 * @return gain at given frequency in dB (clamped to -120..+120)
			 */
			float dB_at_freq (float freq) const;

			/** reset filter state */
			void reset () { _z1 = _z2 = 0.0; }

			void coefficients (double& a1, double& a2, double& b0, double& b1, double& b2) const;
		private:
			void set_vicanek_poles (const double W0, const double Q, const double A = 1.0);
			void calc_vicanek (const double W0, double& A0, double& A1, double& A2, double& phi0, double& phi1, double& phi2);

			double _rate;
			float  _z1, _z2;
			double _a1, _a2;
			double _b0, _b1, _b2;
	};

	class LIBARDOUR_API SpectrumAnalyzer {
	public:
		virtual ~SpectrumAnalyzer () {}
		virtual float power_at_bin (const uint32_t bin, const float gain, bool pink) const = 0;
		virtual float freq_at_bin (const uint32_t bin) const = 0;
	};

	class LIBARDOUR_API FFTSpectrum : public SpectrumAnalyzer {
		public:
			FFTSpectrum (uint32_t window_size, double rate);
			~FFTSpectrum ();

			/** set data to be analyzed and pre-process with hanning window
			 * n_samples + offset must not be larger than the configured window_size
			 *
			 * @param data raw audio data
			 * @param n_samples number of samples to write to analysis buffer
			 * @param offset destination offset
			 */
			void set_data_hann (float const * const data, const uint32_t n_samples, const uint32_t offset = 0);

			/** process current data in buffer */
			void execute ();

			/** query
			 * @param bin the frequency bin 0 .. window_size / 2
			 * @param norm gain factor (set equal to \p bin for 1/f normalization)
			 * @return signal power at given bin (in dBFS)
			 */
			float power_at_bin (const uint32_t bin, const float gain = 1.f, bool pink = false) const;

			float freq_at_bin (const uint32_t bin) const {
				return bin * _fft_freq_per_bin;
			}

		private:
			float* hann_window;

			void init (uint32_t window_size, double rate);
			void reset ();

			uint32_t _fft_window_size;
			uint32_t _fft_data_size;
			double   _fft_freq_per_bin;

			float* _fft_data_in;
			float* _fft_data_out;
			float* _fft_power;

			fftwf_plan _fftplan;
	};

	class LIBARDOUR_API PerceptualAnalyzer : public SpectrumAnalyzer {
		public:
			PerceptualAnalyzer (double rate, int ipsize = 4096);
			~PerceptualAnalyzer ();
			PerceptualAnalyzer (PerceptualAnalyzer const&) = delete;

			class Trace {
				public:
					Trace (int size);
					~Trace ();

					bool     _valid;
					int32_t  _count;
					float   *_data;
			};

			enum ProcessMode {
					MM_NONE,
					MM_PEAK,
					MM_AVER
			};

			enum Speed {
				Rapid,
				Fast,
				Moderate,
				Slow,
				Noise
			};

			enum Warp {
				Bark,
				Medium,
				High
			};

			void set_wfact (float wfact);
			void set_speed (float speed);

			void set_wfact (enum Warp);
			void set_speed (enum Speed);

			void reset ();

			int    fftlen () const { return _fftlen; }
			float* ipdata () const { return _ipdata; }
			Trace* power ()  const { return _power; }
			Trace* peakp ()  const { return _peakp; }
			float  pmax ()   const { return _pmax; }

			/** process current data in buffer */
			void process (int iplen, ProcessMode mode = MM_NONE);

			static double warp_freq (double w, double f);

			float freq_at_bin (const uint32_t bin) const;
			float power_at_bin (const uint32_t bin, const float gain = 1.f, bool pink = false) const;

		private:
			static const int _fftlen = 512;

			void  init ();
			float conv0 (fftwf_complex*);
			float conv1 (fftwf_complex*);

			int              _ipsize;
			int              _icount;
			fftwf_plan       _fftplan;
			float           *_ipdata;
			float           *_warped;
			fftwf_complex   *_trdata;
			Trace           *_power;
			Trace           *_peakp;
			float            _fsamp;
			float            _wfact;
			float            _speed;
			float            _pmax;
			float            _fscale[513];
			float            _bwcorr[513];
	};

	class LIBARDOUR_API StereoCorrelation {
		public:
			StereoCorrelation (float samplerate, float lp_freq = 2e3f, float tc_corr = .3f);

			void process (float const*, float const*, uint32_t n_samples);
			void reset ();
			float read () const;

		private:
			float _zl;
			float _zr;
			float _zlr;
			float _zll;
			float _zrr;
			float _w1;  // lowpass filter coefficient
			float _w2;  // correlation filter coeffient
	};

	class LIBARDOUR_API Generator {
		public:
			Generator ();

			enum Type {
				UniformWhiteNoise,
				GaussianWhiteNoise,
				PinkNoise,
			};

			void run (float *data, const uint32_t n_samples);
			void set_type (Type t);

		private:
			uint32_t randi ();
			float    randf () { return (randi () / 1073741824.f) - 1.f; }
			float    grandf ();

			Type     _type;
			uint32_t _rseed;
			/* pink-noise */
			float _b0, _b1, _b2, _b3, _b4, _b5, _b6;
			/* gaussian white */
			bool _pass;
			float _rn;

	};

} } /* namespace */

namespace PBD {
DEFINE_ENUM_CONVERT(ARDOUR::DSP::PerceptualAnalyzer::Speed);
DEFINE_ENUM_CONVERT(ARDOUR::DSP::PerceptualAnalyzer::Warp);
}
#endif
