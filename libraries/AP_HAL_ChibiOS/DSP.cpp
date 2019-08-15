/*
 * This file is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 * Code by Andy Piper and the betaflight team
 */

#include "AP_HAL_ChibiOS.h"

#if HAL_WITH_DSP

#include <AP_HAL/AP_HAL.h>
#include "DSP.h"
#ifdef DEBUG_FFT
#include <GCS_MAVLink/GCS.h>
#endif
#include <cmath>

using namespace ChibiOS;

#ifdef DEBUG_FFT
#define TIMER_START(timer) uint32_t timer##now = AP_HAL::micros()
#define TIMER_END(timer) timer.time(timer##now)
#else
#define TIMER_START(timer)
#define TIMER_END(timer)
#endif

#define TICK_CYCLE 10

AP_HAL::DSP::FFTWindowState* DSP::fft_init(uint16_t window_size, uint16_t sample_rate) 
{
    uint8_t update_steps = 0;
    switch (window_size) {
    case 32:
    case 64:
        update_steps = 2;
        break;
    case 128:
        update_steps = 3;
        break;
    case 256:
    case 512:
    case 1024:
        update_steps = 6;
        break;
    }
    return new DSP::FFTWindowStateARM(window_size, sample_rate, update_steps);
}

extern "C" {
    void stage_rfft_f32(arm_rfft_fast_instance_f32 *S, float32_t *p, float32_t *pOut);
    void arm_cfft_radix8by2_f32(arm_cfft_instance_f32 *S, float32_t *p1);
    void arm_cfft_radix8by4_f32(arm_cfft_instance_f32 *S, float32_t *p1);
    void arm_radix8_butterfly_f32(float32_t *pSrc, uint16_t fftLen, const float32_t *pCoef, uint16_t twidCoefModifier);
    void arm_bitreversal_32(uint32_t *pSrc, const uint16_t bitRevLen, const uint16_t *pBitRevTable);
}

// Analyse FFT data contained in samples and return it in state->_freq_bins
// This is a state-machine version of arm_rfft_fast_f32() such that each step can be processed separately as required
// The number of cycles to reach a solution is based upon the window size as larger windows mean longer step times
// Experimentally derived values are given as comments in the steps, differences between MCUs may mean more individual steps are required
uint16_t DSP::fft_analyse(AP_HAL::DSP::FFTWindowState* state, const float* samples, uint16_t buffer_index, uint16_t start_bin)
{
    FFTWindowStateARM* fft = (FFTWindowStateARM*)state;
    uint16_t bin_max = 0;

    switch (fft->_window_size) {
    case 32:
    case 64:
        // 2 steps
        bin_max = fft_analyse_window32(fft, samples, buffer_index, start_bin);
        break;
    case 128:
        // 3 steps
        bin_max = fft_analyse_window128(fft, samples, buffer_index, start_bin);
        break;
    case 256:
    case 512:
    case 1024:
        // 6 steps
        bin_max = fft_analyse_window256(fft, samples, buffer_index, start_bin);
        break;
    }
    return bin_max;
}

uint16_t DSP::fft_analyse_window32(FFTWindowStateARM* fft, const float* samples, uint16_t buffer_index, uint16_t start_bin)
{
    uint32_t bin_max = 0;

    switch (fft->_update_step) {
    case STEP_HANNING:
        step_hanning(fft, samples, buffer_index);
        step_arm_cfft_f32(fft);
        break;
    case STEP_BITREVERSAL:
        step_bitreversal(fft);
        step_stage_rfft_f32(fft);
        step_arm_cmplx_mag_f32(fft);
        bin_max = step_calc_frequencies(fft, start_bin);
        break;
    }
    fft->_update_step = fft->_update_step % STEP_COUNT;

    return bin_max;
}

uint16_t DSP::fft_analyse_window128(FFTWindowStateARM* fft, const float* samples, uint16_t buffer_index, uint16_t start_bin)
{
    uint32_t bin_max = 0;

    switch (fft->_update_step) {
    case STEP_HANNING:
        step_hanning(fft, samples, buffer_index);
        step_arm_cfft_f32(fft);
        break;
    case STEP_BITREVERSAL:
        step_bitreversal(fft);
        step_stage_rfft_f32(fft);
        break;
    case STEP_ARM_CMPLX_MAG_F32:
        step_arm_cmplx_mag_f32(fft);
        bin_max = step_calc_frequencies(fft, start_bin);
        break;
    }
    fft->_update_step = fft->_update_step % STEP_COUNT;

    return bin_max;
}

uint16_t DSP::fft_analyse_window256(FFTWindowStateARM* fft, const float* samples, uint16_t buffer_index, uint16_t start_bin)
{
    uint32_t bin_max = 0;

    switch (fft->_update_step) {
    case STEP_HANNING:
        step_hanning(fft, samples, buffer_index);
        break;
    case STEP_ARM_CFFT_F32:
        step_arm_cfft_f32(fft);
        break;
    case STEP_BITREVERSAL:
        step_bitreversal(fft);
        break;
    case STEP_STAGE_RFFT_F32:
        step_stage_rfft_f32(fft);
        break;
    case STEP_ARM_CMPLX_MAG_F32:
        step_arm_cmplx_mag_f32(fft);
        break;
    case STEP_CALC_FREQUENCIES:
        bin_max = step_calc_frequencies(fft, start_bin);
        break;
    }
    fft->_update_step = fft->_update_step % STEP_COUNT;

    return bin_max;
}

void DSP::step_hanning(FFTWindowStateARM* fft, const float* samples, uint16_t buffer_index)
{
    // 5us
    // apply hanning window to gyro samples and store result in _freq_bins
    // hanning starts and ends with 0, could be skipped for minor speed improvement
    const uint16_t ring_buf_idx = fft->_window_size - buffer_index;
    arm_mult_f32(&samples[buffer_index], &fft->_hanning_window[0], &fft->_freq_bins[0], ring_buf_idx);
    if (buffer_index > 0) {
        arm_mult_f32(&samples[0], &fft->_hanning_window[ring_buf_idx], &fft->_freq_bins[ring_buf_idx], buffer_index);
    }
    fft->_update_step++;
}

void DSP::step_arm_cfft_f32(FFTWindowStateARM* fft)
{
    arm_cfft_instance_f32 *Sint = &(fft->_fft_instance.Sint);
    Sint->fftLen = fft->_fft_instance.fftLenRFFT / 2;

    TIMER_START(_arm_cfft_f32_timer);

    switch (fft->_bin_count) {
    case 16: // window 32
        // 16us (BF)
        // 5us F7
    case 128: // window 256
        // 37us F7
        arm_cfft_radix8by2_f32(Sint, fft->_freq_bins);
        break;
    case 32: // window 64
        // 35us (BF)
        // 10us F7
    case 256: // window 512
        // 66us F7
        arm_cfft_radix8by4_f32(Sint, fft->_freq_bins);
        break;
    case 64: // This does not work - seems to take too long, or hangs
        // 70us BF
    case 512: // window 1024
        // 152us F7
        arm_radix8_butterfly_f32(fft->_freq_bins, fft->_bin_count, Sint->pTwiddle, 1);
        break;
    }

    TIMER_END(_arm_cfft_f32_timer);
    fft->_update_step++;
}

void DSP::step_bitreversal(FFTWindowStateARM* fft)
{
    TIMER_START(_bitreversal_timer);
    // 6us (BF)
    // 2us 32 F7
    // 3us 64 F7
    // 10us 256 F7
    // 22us 512 F7
    // 42us 1024 F7
    arm_bitreversal_32((uint32_t *)fft->_freq_bins, fft->_fft_instance.Sint.bitRevLength, fft->_fft_instance.Sint.pBitRevTable);

    TIMER_END(_bitreversal_timer);
    fft->_update_step++;
}

void DSP::step_stage_rfft_f32(FFTWindowStateARM* fft)
{
    TIMER_START(_stage_rfft_f32_timer);
    // 14us (BF)
    // 2us 32 F7
    // 5us 64 F7
    // 21us 256 F7
    // 35us 512 F7
    // 76us 1024 F7
    // this does not work in place => _freq_bins AND _rfft_data needed
    stage_rfft_f32(&fft->_fft_instance, fft->_freq_bins, fft->_rfft_data);

    TIMER_END(_stage_rfft_f32_timer);
    fft->_update_step++;
}

void DSP::step_arm_cmplx_mag_f32(FFTWindowStateARM* fft)
{
    TIMER_START(_arm_cmplx_mag_f32_timer);
    // 8us (BF)
    // 4us 32 F7
    // 7us 64 F7
    // 29us 256 F7
    // 55us 512 F7
    // 131us 1024 F7
    // General case for the magnitudes - see https://stackoverflow.com/questions/42299932/dsp-libraries-rfft-strange-results
    // The frequency of each of those frequency components are given by k*fs/N
    arm_cmplx_mag_f32(&fft->_rfft_data[2], &fft->_freq_bins[1], fft->_bin_count - 1);
    fft->_freq_bins[0] = abs(fft->_rfft_data[0]);               // DC
    fft->_freq_bins[fft->_bin_count] = abs(fft->_rfft_data[1]); // Nyquist
    fft->_freq_bins[fft->_bin_count + 1] = 0;                   // So that interpolation works correctly

    TIMER_END(_arm_cmplx_mag_f32_timer);
    fft->_update_step++;
}
    
uint16_t DSP::step_calc_frequencies(FFTWindowStateARM* fft, uint16_t start_bin)
{
    float max_value = 0;
    uint32_t bin_max = 0;

    arm_max_f32(&fft->_freq_bins[start_bin], (fft->_bin_count + 1) - start_bin, &max_value, &bin_max);
    bin_max += start_bin;

#ifdef DEBUG_FFT
    _output_count++;
    // outputs at approx 1hz
    if (_output_count % (400 / fft->_update_steps) == 0) {
        gcs().send_text(MAV_SEVERITY_WARNING, "FFT: t1:%luus, t2:%luus, t3:%luus, t4:%luus",
                        _arm_cfft_f32_timer._timer_avg, _bitreversal_timer._timer_avg, _stage_rfft_f32_timer._timer_avg, _arm_cmplx_mag_f32_timer._timer_avg);
    }
#endif
    fft->_update_step++;

    return bin_max;
}

DSP::FFTWindowStateARM::FFTWindowStateARM(uint16_t window_size, uint16_t sample_rate, uint8_t update_steps)
    : AP_HAL::DSP::FFTWindowState::FFTWindowState(window_size, sample_rate, update_steps)
{
    arm_rfft_fast_init_f32(&_fft_instance, _window_size);

    _rfft_data = new float[_window_size];
    _hanning_window = new float[_window_size];

    for (uint16_t i = 0; i < _window_size; i++) {
        _hanning_window[i] = (0.5f - 0.5f * cosf(2 * M_PI * i / (_window_size - 1)));
    }
}

DSP::FFTWindowStateARM::~FFTWindowStateARM()
{
    delete[] _rfft_data;
    delete[] _hanning_window;
}

#ifdef DEBUG_FFT
 void DSP::StepTimer::time(uint32_t start) 
 {
    _timer_total += (AP_HAL::micros() - start);
    _time_ticks = (_time_ticks + 1) % TICK_CYCLE;
    if (_time_ticks == 0) {
        _timer_avg = _timer_total / TICK_CYCLE;
        _timer_total = 0;
    }
}
#endif

#endif
