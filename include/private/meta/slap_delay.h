/*
 * Copyright (C) 2021 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2021 Vladimir Sadovnikov <sadko4u@gmail.com>
 *
 * This file is part of lsp-plugins-slap-delay
 * Created on: 3 авг. 2021 г.
 *
 * lsp-plugins-slap-delay is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * lsp-plugins-slap-delay is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with lsp-plugins-slap-delay. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef PRIVATE_META_SLAP_DELAY_H_
#define PRIVATE_META_SLAP_DELAY_H_

#include <lsp-plug.in/plug-fw/meta/types.h>
#include <lsp-plug.in/plug-fw/const.h>


namespace lsp
{
    namespace meta
    {
        struct slap_delay_metadata
        {
            static constexpr float  DISTANCE_MIN        = 0.0f;
            static constexpr float  DISTANCE_MAX        = 400.0f;
            static constexpr float  DISTANCE_STEP       = 0.01;
            static constexpr float  DISTANCE_DFL        = 0.0f;

            static constexpr float  TIME_MIN            = 0.0f;
            static constexpr float  TIME_MAX            = 1000.0f;
            static constexpr float  TIME_STEP           = 0.01f;
            static constexpr float  TIME_DFL            = 0.0f;

            static constexpr float  DENOMINATOR_MIN     = 1.0f;
            static constexpr float  DENOMINATOR_MAX     = 64.0f;
            static constexpr float  DENOMINATOR_STEP    = 1.0f;
            static constexpr float  DENOMINATOR_DFL     = 4.0f;

            static constexpr float  FRACTION_MIN        = 0.0f;
            static constexpr float  FRACTION_MAX        = 2.0f;
            static constexpr float  FRACTION_STEP       = 1.0f / 64.0f;
            static constexpr float  FRACTION_DFL        = 0.0f;

            static constexpr float  TEMPO_MIN           = 20.0f;
            static constexpr float  TEMPO_MAX           = 360.0f;
            static constexpr float  TEMPO_STEP          = 0.1f;
            static constexpr float  TEMPO_DFL           = 120.0f;

            static constexpr float  PRED_TIME_MIN       = 0.0f;
            static constexpr float  PRED_TIME_MAX       = 200.0f;
            static constexpr float  PRED_TIME_STEP      = 0.01f;
            static constexpr float  PRED_TIME_DFL       = 0.0f;

            static constexpr float  STRETCH_MIN         = 25.0f;
            static constexpr float  STRETCH_MAX         = 400.0f;
            static constexpr float  STRETCH_STEP        = 0.1f;
            static constexpr float  STRETCH_DFL         = 100.0f;

            static constexpr float  TEMPERATURE_MIN     = -60;
            static constexpr float  TEMPERATURE_MAX     = +60;
            static constexpr float  TEMPERATURE_DFL     = 20.0;
            static constexpr float  TEMPERATURE_STEP    = 0.1;

            static constexpr float  BAND_GAIN_MIN       = GAIN_AMP_M_24_DB;
            static constexpr float  BAND_GAIN_MAX       = GAIN_AMP_P_24_DB;
            static constexpr float  BAND_GAIN_STEP      = 0.025f;
            static constexpr float  BAND_GAIN_DFL       = GAIN_AMP_0_DB;

            static constexpr float  LOW_CUT_MIN         = SPEC_FREQ_MIN;
            static constexpr float  LOW_CUT_MAX         = 1000.0f;
            static constexpr float  LOW_CUT_STEP        = 0.01f;
            static constexpr float  LOW_CUT_DFL         = 100.0f;

            static constexpr float  HIGH_CUT_MIN        = 1000.0f;
            static constexpr float  HIGH_CUT_MAX        = SPEC_FREQ_MAX;
            static constexpr float  HIGH_CUT_STEP       = 0.01f;
            static constexpr float  HIGH_CUT_DFL        = 8000.0f;

            static constexpr size_t EQ_BANDS            = 5;

            static constexpr size_t MAX_PROCESSORS      = 16;

            enum op_modes_t
            {
                OP_MODE_NONE,
                OP_MODE_TIME,
                OP_MODE_DISTANCE,
                OP_MODE_NOTE
            };
        };

        extern const meta::plugin_t slap_delay_mono;
        extern const meta::plugin_t slap_delay_stereo;
    } // namespace meta
} // namespace lsp


#endif /* PRIVATE_META_SLAP_DELAY_H_ */
