/*
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

    Code by Andy Piper
 */
#pragma once

#include <AP_Common/AP_Common.h>
#include <AP_HAL/AP_HAL.h>
#include <AP_Param/AP_Param.h>
#include <AP_Math/AP_Math.h>
#include <AP_InertialSensor/AP_InertialSensor.h>

#define XYZ_AXIS_COUNT              3
#define FFT_UPDATE_BUDGET_MICROS    175 // the budgeted update period
//#define DEBUG_FFT
//#define DEBUG_FFT_TIMING

// a library that leverages the HAL DSP support to perform FFT analysis on gyro samples
class AP_GyroFFT
{
public:
    AP_GyroFFT();

    // Do not allow copies
    AP_GyroFFT(const AP_GyroFFT &other) = delete;
    AP_GyroFFT &operator=(const AP_GyroFFT&) = delete;

    void init(uint32_t target_looptime, AP_InertialSensor& ins);

    // cycle through the FFT steps
    void update();
    // capture gyro values at the appropriate update rate
    void sample_gyros();
    // check at startup that standard frequencies can be detected
    bool calibration_check();
    // called when hovering to determine the average peak frequency and reference value
    void update_freq_hover(float dt, float throttle_out);
    // called to save the average peak frequency and reference value
    void save_params_on_disarm();

    // detected peak frequency filtered at 1/3 the update rate
    Vector3f get_noise_center_freq_hz() const { return _center_freq_hz_filtered; }
    // energy of the background noise
    Vector3f get_noise_ref_energy() const { return _ref_energy; }
    // detected peak frequency weighted by energy
    float get_weighted_noise_center_freq_hz() const;
    // detected peak frequency
    Vector3f get_raw_noise_center_freq_hz() const { return _center_freq_hz; }
    // energy of the detected peak frequency
    Vector3f get_center_freq_energy() const { return _center_freq_energy; }
    // index of the FFT bin containing the detected peak frequency
    Vector3<uint8_t> get_center_freq_bin() const { return _center_freq_bin; }

    // total number of cycles where the time budget was not met
    uint32_t get_total_overrun_cycles() const { return _overrun_cycles; }
    // average cycle time where the time budget was not met
    uint32_t get_average_overrun() const { return _overrun_total / _overrun_cycles; }

    static const struct AP_Param::GroupInfo var_info[];
    static AP_GyroFFT *get_singleton() { return _singleton; }

private:
    // calculate the peak noise frequency
    void calculate_noise(uint16_t bin_max);
    // update the estimation of the background noise energy
    void update_ref_energy();
    // interpolate between frequency bins using simple method
    float calculate_simple_center_freq(uint8_t bin_max);
    // interpolate between frequency bins using jains method
    float calculate_jains_estimator_center_freq(uint8_t k);
    // test frequency detection for all of the allowable bins
    float self_test_bin_frequencies();
    // detect the provided frequency
    float self_test(float frequency);

    // number of sampeles needed before a new frame can be processed
    uint16_t _samples_per_frame;
    // downsampled gyro data circular buffer for frequency analysis
    float* _downsampled_gyro_data[XYZ_AXIS_COUNT];
    // downsampled gyro data circular buffer index frequency analysis
    uint16_t _circular_buffer_idx;
    // number of collected unprocessed gyro samples
    uint16_t _sample_count;
    // accumulator for sampled gyro data
    Vector3f _oversampled_gyro_accum;
    // multiplier for gyro samples
    float _multiplier;

    // state of the FFT engine
    AP_HAL::DSP::FFTWindowState* _state;
    // update state machine step information
    uint8_t _update_axis;
    // the number of cycles required to have a proper noise reference
    uint8_t _noise_cycles;

    // energy of the detected peak frequency
    Vector3f _center_freq_energy;
    // detected peak frequency
    Vector3f _center_freq_hz;
    // bin of detected poeak frequency
    Vector3<uint8_t> _center_freq_bin;
    // filtered version of the peak frequency
    Vector3f _center_freq_hz_filtered;
    // noise base of the gyros
    Vector3f _ref_energy;
    // smoothing filter on the output
    LowPassFilter2pFloat _center_freq_filter[XYZ_AXIS_COUNT];

    // performance counters
    uint32_t _overrun_cycles;
    uint32_t _overrun_total;
    uint32_t _overrun_max;

    // configured sampling rate
    uint16_t _fft_sampling_rate_hz;
    // configured start bin based on min hz
    uint8_t _fft_start_bin;
    // number of cycles without a detected signal
    uint8_t _missed_cycles; 
    // axes that still require noise calibration
    uint8_t _noise_needs_calibration : 3;
    // minimum frequency of the detection window
    AP_Int16 _fft_min_hz;
    // maximum frequency of the detection window
    AP_Int16 _fft_max_hz;
    // size of the FFT window
    AP_Int16 _window_size;
    // percentage overlap of FFT windows
    AP_Float _window_overlap;
    AP_Int8 _enable;
    // gyro rate sampling or cycle divider
    AP_Int8 _sample_mode;
    // learned throttle reference for the hover frequency
    AP_Float _throttle_ref;
    // learned hover filter frequency
    AP_Float _freq_hover;
    AP_Int8 _track_mode;
    AP_InertialSensor* _ins;
#if defined(DEBUG_FFT) || defined(DEBUG_FFT_TIMING)
    uint32_t _output_count;
#endif

    static AP_GyroFFT *_singleton;
};

namespace AP {
    AP_GyroFFT *fft();
};
