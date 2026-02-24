/*
 * Copyright (C) 2026 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2026 Vladimir Sadovnikov <sadko4u@gmail.com>
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

#ifndef PRIVATE_PLUGINS_SLAP_DELAY_H_
#define PRIVATE_PLUGINS_SLAP_DELAY_H_

#include <lsp-plug.in/plug-fw/plug.h>
#include <lsp-plug.in/dsp-units/ctl/Bypass.h>
#include <lsp-plug.in/dsp-units/util/RawRingBuffer.h>
#include <lsp-plug.in/dsp-units/filters/Equalizer.h>

#include <private/meta/slap_delay.h>

namespace lsp
{
    namespace plugins
    {
        /**
         * Slap Delay Plugin series
         */
        class slap_delay: public plug::Module
        {
            protected:
                typedef struct mono_processor_t
                {
                    dspu::RawRingBuffer     sBuffer;    // Ring buffer for the delay data
                    dspu::Equalizer         sEqualizer; // Delay equalizer

                    float                   fGain[2];   // Amount of gain for left and right input channels
                    float                   fFeedback;  // Feedback gain
                } mono_processor_t;

                typedef struct processor_t
                {
                    mono_processor_t       *vDelay;     // Delay processors

                    size_t                  nDelay;     // Delay
                    size_t                  nNewDelay;  // New delay
                    uint32_t                nChangeReq; // Change request
                    uint32_t                nChangeResp;// Change response
                    uint32_t                nMode;      // Current operating mode

                    plug::IPort            *pMode;      // Operating mode port
                    plug::IPort            *pEq;        // Equalizer
                    plug::IPort            *pTime;      // Delay in time units
                    plug::IPort            *pDistance;  // Delay in distance units
                    plug::IPort            *pFrac;      // Fraction
                    plug::IPort            *pDenom;     // Denominator
                    plug::IPort            *pPan[2];    // Pan of left and right input channels
                    plug::IPort            *pBalance;   // The balance between left and right channels
                    plug::IPort            *pFeedback;  // Feedback amount
                    plug::IPort            *pGain;      // Gain of the delay line
                    plug::IPort            *pLowCut;    // Low-cut flag
                    plug::IPort            *pLowFreq;   // Low-cut frequency
                    plug::IPort            *pHighCut;   // High-cut flag
                    plug::IPort            *pHighFreq;  // Low-cut frequency
                    plug::IPort            *pSolo;      // Solo control
                    plug::IPort            *pMute;      // Mute control
                    plug::IPort            *pPhase;     // Phase control
                    plug::IPort            *pFreqGain[meta::slap_delay_metadata::EQ_BANDS];      // Gain for each band of the Equalizer
                } processor_t;

                typedef struct channel_t
                {
                    dspu::Bypass            sBypass;    // Bypass
                    float                   fGain[2];   // Panning gain
                    float                  *vRender;    // Rendering buffer
                    float                  *vTemp;      // Temporary buffer for processing
                    float                  *vOut;       // Output buffer
                    plug::IPort            *pOut;       // Output port
                } channel_t;

                typedef struct input_t
                {
                    float                  *vIn;        // Input data
                    plug::IPort            *pIn;        // Input port
                    plug::IPort            *pPan;       // Panning
                } input_t;

                typedef struct proc_state_t
                {
                    mono_processor_t       *vProcessors;// Processors
                    size_t                  nLength;    // Length
                    size_t                  nSampleRate;// Sample rate
                    uint32_t                nChangeId;  // Serial state
                } proc_state_t;

                class ProcessorAllocator: public ipc::ITask
                {
                    private:
                        slap_delay         *pCore;
                        proc_state_t        sState;

                    public:
                        explicit ProcessorAllocator(slap_delay *core);
                        virtual ~ProcessorAllocator() override;

                    public:
                        virtual status_t    run() override;

                    public:
                        inline proc_state_t &state() { return sState; }
                };

            protected:
                size_t              nInputs;        // Mono/Stereo mode flag
                input_t            *vInputs;        // Inputs

                processor_t         vProcessors[meta::slap_delay_metadata::MAX_PROCESSORS];     // Processors
                ProcessorAllocator *vAllocators[meta::slap_delay_metadata::MAX_PROCESSORS];     // Allocation tasks
                channel_t           vChannels[2];

                ipc::IExecutor     *pExecutor;
                size_t              nDelayLength;   // Maximum delay length
                bool                bMono;          // Mono output flag

                plug::IPort        *pBypass;        // Bypass
                plug::IPort        *pTemp;          // Temperature
                plug::IPort        *pDry;           // Dry signal amount
                plug::IPort        *pDryMute;       // Dry mute
                plug::IPort        *pWet;           // Wet signal amount
                plug::IPort        *pWetMute;       // Wet mute
                plug::IPort        *pDryWet;        // Dry/Wet balance
                plug::IPort        *pOutGain;       // Output gain
                plug::IPort        *pMono;          // Mono output
                plug::IPort        *pPred;          // Pre-delay
                plug::IPort        *pBalance;       // Balance
                plug::IPort        *pStretch;       // Time stretch
                plug::IPort        *pTempo;         // Tempo
                plug::IPort        *pSync;          // Sync tempo
                plug::IPort        *pRamping;       // Ramping mode

                uint8_t            *vData;          // Allocated data

            protected:
                void                do_destroy();
                void                sync_processors_state();
                void                update_processor_state(processor_t *p);
                status_t            manage_request(proc_state_t & req);

            protected:
                static void         process_const_delay(
                    float *dst, const float *src,
                    mono_processor_t *mp,
                    size_t delay, size_t samples);

                static void         process_varying_delay(
                    float *dst, const float *src,
                    mono_processor_t *mp,
                    size_t delay, float delta,
                    size_t step, size_t samples);

                static void         delete_processors(mono_processor_t *vp, size_t count);

            public:
                slap_delay(const meta::plugin_t *metadata);
                slap_delay(const slap_delay &) = delete;
                slap_delay(slap_delay &&) = delete;
                virtual ~slap_delay() override;

                slap_delay & operator = (const slap_delay &) = delete;
                slap_delay & operator = (slap_delay &&) = delete;

            public:
                virtual void        init(plug::IWrapper *wrapper, plug::IPort **ports) override;
                virtual void        destroy() override;

                virtual bool        set_position(const plug::position_t *pos) override;
                virtual void        update_settings() override;
                virtual void        update_sample_rate(long sr) override;

                virtual void        process(size_t samples) override;
                virtual void        dump(dspu::IStateDumper *v) const override;
        };
    } /* namespace plugins */
} /* namespace lsp */

#endif /* PRIVATE_PLUGINS_SLAP_DELAY_H_ */
