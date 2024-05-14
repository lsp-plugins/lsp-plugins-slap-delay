/*
 * Copyright (C) 2024 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2024 Vladimir Sadovnikov <sadko4u@gmail.com>
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

#include <lsp-plug.in/common/alloc.h>
#include <lsp-plug.in/common/debug.h>
#include <lsp-plug.in/dsp/dsp.h>
#include <lsp-plug.in/dsp-units/units.h>
#include <lsp-plug.in/plug-fw/meta/func.h>
#include <lsp-plug.in/shared/debug.h>

#include <private/plugins/slap_delay.h>

namespace lsp
{
    namespace plugins
    {
        static constexpr size_t BUFFER_SIZE             = 0x400;
        static constexpr size_t DELAY_PACKET_PROCESSING = 16;

        //---------------------------------------------------------------------
        // Plugin factory
        static const meta::plugin_t *plugins[] =
        {
            &meta::slap_delay_mono,
            &meta::slap_delay_stereo
        };

        static plug::Module *plugin_factory(const meta::plugin_t *meta)
        {
            return new slap_delay(meta);
        }

        static plug::Factory factory(plugin_factory, plugins, 2);


        static const float band_freqs[] =
        {
            60.0f,
            300.0f,
            1000.0f,
            6000.0f
        };

        //-------------------------------------------------------------------------
        slap_delay::slap_delay(const meta::plugin_t *metadata): plug::Module(metadata)
        {
            nInputs         = 0;
            for (const meta::port_t *p = metadata->ports; p->id != NULL; ++p)
                if ((meta::is_in_port(p)) && (meta::is_audio_port(p)))
                    ++nInputs;

            vInputs         = NULL;

            bMono           = false;

            for (size_t i=0; i<meta::slap_delay_metadata::MAX_PROCESSORS; ++i)
            {
                processor_t *p      = &vProcessors[i];

                for (size_t j=0; j<2; ++j)
                {
                    mono_processor_t *mp = &p->vDelay[j];

                    mp->fGain[0]        = 0.0f;
                    mp->fGain[1]        = 0.0f;
                    mp->fFeedback       = 0.0f;
                }

                p->nDelay           = 0;
                p->nNewDelay        = 0;
                p->nMode            = 0;

                p->pMode            = NULL;
                p->pEq              = NULL;
                p->pTime            = NULL;
                p->pDistance        = NULL;
                p->pFrac            = NULL;
                p->pDenom           = NULL;
                p->pPan[0]          = NULL;
                p->pPan[1]          = NULL;
                p->pFeedback        = NULL;
                p->pGain            = NULL;
                p->pLowCut          = NULL;
                p->pLowFreq         = NULL;
                p->pHighCut         = NULL;
                p->pHighFreq        = NULL;
                p->pSolo            = NULL;
                p->pMute            = NULL;
                p->pPhase           = NULL;

                for (size_t j=0; j<meta::slap_delay_metadata::EQ_BANDS; ++j)
                    p->pFreqGain[j]     = NULL;
            }

            for (size_t i=0; i<2; ++i)
            {
                channel_t *c        = &vChannels[i];

                c->fGain[0]         = 0.0f;
                c->fGain[1]         = 0.0f;
                c->vRender          = NULL;
                c->vTemp            = NULL;
                c->vOut             = NULL;
                c->pOut             = NULL;
            }

            pBypass         = NULL;
            pTemp           = NULL;
            pDry            = NULL;
            pDryMute        = NULL;
            pWet            = NULL;
            pWetMute        = NULL;
            pDryWet         = NULL;
            pOutGain        = NULL;
            pMono           = NULL;
            pPred           = NULL;
            pStretch        = NULL;
            pTempo          = NULL;
            pSync           = NULL;
            pRamping        = NULL;

            vData           = NULL;
        }

        slap_delay::~slap_delay()
        {
            do_destroy();
        }

        void slap_delay::init(plug::IWrapper *wrapper, plug::IPort **ports)
        {
            plug::Module::init(wrapper, ports);

            // Allocate inputs
            vInputs         = new input_t[nInputs];
            if (vInputs == NULL)
                return;

            // Allocate buffers
            size_t alloc    = BUFFER_SIZE * 4;
            float *ptr      = alloc_aligned<float>(vData, alloc * sizeof(float), DEFAULT_ALIGN);
            if (ptr == NULL)
                return;

            // Initialize inputs
            for (size_t i=0; i<nInputs; ++i)
            {
                vInputs[i].vIn      = NULL;
                vInputs[i].pIn      = NULL;
                vInputs[i].pPan     = NULL;
            }

            // Initialize channels
            for (size_t i=0; i<2; ++i)
            {
                channel_t *c        = &vChannels[i];

                c->vOut             = NULL;
                c->pOut             = NULL;
                c->vRender          = advance_ptr<float>(ptr, BUFFER_SIZE);
                c->vTemp            = advance_ptr<float>(ptr, BUFFER_SIZE);
            }

            for (size_t i=0; i<meta::slap_delay_metadata::MAX_PROCESSORS; ++i)
            {
                processor_t    *p   = &vProcessors[i];

                p->nDelay           = 0;
                p->nNewDelay        = 0;
                p->nMode            = M_OFF;

                p->pMode            = NULL;
                p->pTime            = NULL;
                p->pDistance        = NULL;
                p->pPan[0]          = NULL;
                p->pPan[1]          = NULL;
                p->pGain            = NULL;
                p->pLowCut          = NULL;
                p->pLowFreq         = NULL;
                p->pHighCut         = NULL;
                p->pHighFreq        = NULL;
                p->pSolo            = NULL;
                p->pMute            = NULL;
                p->pPhase           = NULL;

                for (size_t j=0; j<meta::slap_delay_metadata::EQ_BANDS; ++j)
                    p->pFreqGain[j]     = NULL;

                for (size_t j=0; j<2; ++j)
                {
                    mono_processor_t *mp = &p->vDelay[j];

                    mp->sEqualizer.init(meta::slap_delay_metadata::EQ_BANDS + 2, 0);
                    mp->sEqualizer.set_mode(dspu::EQM_IIR);

                    mp->bClear          = true;
                }
            }

            lsp_assert(ptr <= reinterpret_cast<float *>(&vData[alloc * sizeof(float) + DEFAULT_ALIGN]));

            // Bind ports
            size_t port_id = 0;

            lsp_trace("Binding audio ports");
            for (size_t i=0; i<nInputs; ++i)
                BIND_PORT(vInputs[i].pIn);

            for (size_t i=0; i<2; ++i)
                BIND_PORT(vChannels[i].pOut);

            // Bind common ports
            lsp_trace("Binding common ports");
            BIND_PORT(pBypass);
            SKIP_PORT("Delay selector"); // Skip delay selector
            BIND_PORT(pTemp);
            BIND_PORT(pPred);
            BIND_PORT(pStretch);
            BIND_PORT(pTempo);
            BIND_PORT(pSync);
            BIND_PORT(pRamping);

            for (size_t i=0; i<nInputs; ++i)
                BIND_PORT(vInputs[i].pPan);

            BIND_PORT(pDry);
            BIND_PORT(pDryMute);
            BIND_PORT(pWet);
            BIND_PORT(pWetMute);
            BIND_PORT(pDryWet);
            BIND_PORT(pMono);
            BIND_PORT(pOutGain);

            // Bind processor ports
            for (size_t i=0; i<meta::slap_delay_metadata::MAX_PROCESSORS; ++i)
            {
                processor_t *p      = &vProcessors[i];

                BIND_PORT(p->pMode);
                for (size_t j=0; j<nInputs; ++j)
                    BIND_PORT(p->pPan[j]);

                BIND_PORT(p->pSolo);
                BIND_PORT(p->pMute);
                BIND_PORT(p->pPhase);
                BIND_PORT(p->pTime);
                BIND_PORT(p->pDistance);
                BIND_PORT(p->pFrac);
                BIND_PORT(p->pDenom);
                BIND_PORT(p->pEq);
                BIND_PORT(p->pLowCut);
                BIND_PORT(p->pLowFreq);
                BIND_PORT(p->pHighCut);
                BIND_PORT(p->pHighFreq);

                for (size_t j=0; j<meta::slap_delay_metadata::EQ_BANDS; ++j)
                    BIND_PORT(p->pFreqGain[j]);

                BIND_PORT(p->pFeedback);
                BIND_PORT(p->pGain);
            }
        }

        void slap_delay::destroy()
        {
            plug::Module::destroy();
            do_destroy();
        }

        void slap_delay::do_destroy()
        {
            if (vInputs != NULL)
            {
                delete [] vInputs;
                vInputs = NULL;
            }

            for (size_t i=0; i<meta::slap_delay_metadata::MAX_PROCESSORS; ++i)
            {
                processor_t *p      = &vProcessors[i];
                for (size_t j=0; j<2; ++j)
                {
                    mono_processor_t *mp = &p->vDelay[j];

                    mp->sBuffer.destroy();
                    mp->sEqualizer.destroy();
                }
            }

            free_aligned(vData);
        }

        bool slap_delay::set_position(const plug::position_t *pos)
        {
            return pos->beatsPerMinute != pWrapper->position()->beatsPerMinute;
        }

        void slap_delay::update_settings()
        {
            float out_gain      = pOutGain->value();
            float g_dry         = (pDryMute->value() >= 0.5f) ? 0.0f : pDry->value();
            float g_wet         = (pWetMute->value() >= 0.5f) ? 0.0f : pWet->value();
            float drywet        = pDryWet->value() * 0.01f;
            float dry_gain      = (g_dry * drywet + 1.0f - drywet) * out_gain;
            float wet_gain      = g_wet * drywet * out_gain;

            float d_delay       = 1.0f / dspu::sound_speed(pTemp->value()); // 1 / ss [m/s] = d_delay [s/m]
            float p_delay       = pPred->value(); // Pre-delay value
            float stretch       = pStretch->value() * 0.01;
            bool bypass         = pBypass->value() >= 0.5f;
            bool has_solo       = false;
            bMono               = pMono->value() >= 0.5f;
            bool ramping        = pRamping->value() >= 0.5f;

            for (size_t i=0; i<2; ++i)
                vChannels[i].sBypass.set_bypass(bypass);

            // Check that solo is enabled
            for (size_t i=0; i<meta::slap_delay_metadata::MAX_PROCESSORS; ++i)
                if (vProcessors[i].pSolo->value() >= 0.5f)
                {
                    has_solo        = true;
                    break;
                }

            if (nInputs == 1)
            {
                float pan               = vInputs[0].pPan->value();
                vChannels[0].fGain[0]   = (100.0f - pan) * 0.005f * dry_gain;
                vChannels[0].fGain[1]   = 0.0f;
                vChannels[1].fGain[0]   = (100.0f + pan) * 0.005f * dry_gain;
                vChannels[1].fGain[1]   = 0.0f;
            }
            else
            {
                float pan_l             = vInputs[0].pPan->value();
                float pan_r             = vInputs[1].pPan->value();

                vChannels[0].fGain[0]   = (100.0f - pan_l) * 0.005f * dry_gain;
                vChannels[0].fGain[1]   = (100.0f - pan_r) * 0.005f * dry_gain;
                vChannels[1].fGain[0]   = (100.0f + pan_l) * 0.005f * dry_gain;
                vChannels[1].fGain[1]   = (100.0f + pan_r) * 0.005f * dry_gain;
            }

            for (size_t i=0; i<meta::slap_delay_metadata::MAX_PROCESSORS; ++i)
            {
                processor_t *p      = &vProcessors[i];

                // Determine mode
                bool eq_on          = p->pEq->value() >= 0.5f;
                bool low_on         = p->pLowCut->value() >= 0.5f;
                bool high_on        = p->pHighCut->value() >= 0.5f;
                dspu::equalizer_mode_t eq_mode = (eq_on || low_on || high_on) ? dspu::EQM_IIR : dspu::EQM_BYPASS;

                size_t old_mode     = p->nMode;
                p->nMode            = p->pMode->value();

                if (p->nMode == meta::slap_delay_metadata::OP_MODE_TIME)
                    p->nNewDelay        = dspu::millis_to_samples(fSampleRate, p->pTime->value() * stretch + p_delay);
                else if (p->nMode == meta::slap_delay_metadata::OP_MODE_DISTANCE)
                    p->nNewDelay        = dspu::seconds_to_samples(fSampleRate, p->pDistance->value() * d_delay * stretch + p_delay * 0.001f);
                else if (p->nMode == meta::slap_delay_metadata::OP_MODE_NOTE)
                {
                    float tempo         = (pSync->value() >= 0.5f) ? pWrapper->position()->beatsPerMinute : pTempo->value();
                    if (tempo < meta::slap_delay_metadata::TEMPO_MIN)
                        tempo               = meta::slap_delay_metadata::TEMPO_MIN;
                    else if (tempo > meta::slap_delay_metadata::TEMPO_MAX)
                        tempo               = meta::slap_delay_metadata::TEMPO_MAX;

                    float delay         = (240.0f * p->pFrac->value()) / tempo;
                    p->nNewDelay        = dspu::seconds_to_samples(fSampleRate, delay * stretch + p_delay * 0.001f);
                }
                else
                    p->nNewDelay        = 0;

                if (!ramping)
                    p->nDelay           = p->nNewDelay;

                lsp_trace("p[%d].nDelay     = %d", int(i), int(p->nDelay));
                lsp_trace("p[%d].nNewDelay  = %d", int(i), int(p->nNewDelay));

                // Calculate delay gain
                float delay_gain    = (p->pMute->value() >= 0.5f) ? 0.0f : p->pGain->value() * wet_gain;
                if ((has_solo) && (p->pSolo->value() < 0.5f))
                    delay_gain          = 0.0f;
                if (p->pPhase->value() >= 0.5f)
                    delay_gain          = -delay_gain;

                const float feedback    = p->pFeedback->value();

                // Apply panning parameters
                if (nInputs == 1)
                {
                    float pan               = p->pPan[0]->value();
                    p->vDelay[0].fGain[0]   = ((100.0f - pan) * 0.005f) * delay_gain;
                    p->vDelay[0].fGain[1]   = ((100.0f + pan) * 0.005f) * delay_gain;
                    p->vDelay[0].fFeedback  = feedback;
                    p->vDelay[1].fGain[0]   = 0.0f;
                    p->vDelay[1].fGain[1]   = 0.0f;
                    if ((old_mode == meta::slap_delay_metadata::OP_MODE_NONE) && (old_mode != p->nMode))
                    {
                        p->vDelay[0].bClear     = true;
                        p->vDelay[0].sBuffer.reset();
                    }
                }
                else
                {
                    float pan_l             = p->pPan[0]->value();
                    float pan_r             = p->pPan[1]->value();

                    p->vDelay[0].fGain[0]   = (100.0f - pan_l) * 0.005f * delay_gain;
                    p->vDelay[0].fGain[1]   = (100.0f - pan_r) * 0.005f * delay_gain;
                    p->vDelay[0].fFeedback  = feedback;
                    p->vDelay[1].fGain[0]   = (100.0f + pan_l) * 0.005f * delay_gain;
                    p->vDelay[1].fGain[1]   = (100.0f + pan_r) * 0.005f * delay_gain;
                    p->vDelay[1].fFeedback  = feedback;

                    if ((old_mode == meta::slap_delay_metadata::OP_MODE_NONE) && (old_mode != p->nMode))
                    {
                        p->vDelay[0].bClear     = true;
                        p->vDelay[1].bClear     = true;

                        p->vDelay[0].sBuffer.reset();
                        p->vDelay[1].sBuffer.reset();
                    }
                }

                // Update equalizer settings
                for (size_t j=0; j<2; ++j)
                {
                    // Update equalizer
                    dspu::Equalizer *eq     = &p->vDelay[j].sEqualizer;
                    eq->set_mode(eq_mode);

                    if (eq_mode == dspu::EQM_BYPASS)
                        continue;

                    dspu::filter_params_t fp;
                    size_t band     = 0;

                    // Set-up parametric equalizer
                    while (band < meta::slap_delay_metadata::EQ_BANDS)
                    {
                        if (band == 0)
                        {
                            fp.fFreq        = band_freqs[band];
                            fp.fFreq2       = fp.fFreq;
                            fp.nType        = (eq_on) ? dspu::FLT_MT_LRX_LOSHELF : dspu::FLT_NONE;
                        }
                        else if (band == (meta::slap_delay_metadata::EQ_BANDS - 1))
                        {
                            fp.fFreq        = band_freqs[band-1];
                            fp.fFreq2       = fp.fFreq;
                            fp.nType        = (eq_on) ? dspu::FLT_MT_LRX_HISHELF : dspu::FLT_NONE;
                        }
                        else
                        {
                            fp.fFreq        = band_freqs[band-1];
                            fp.fFreq2       = band_freqs[band];
                            fp.nType        = (eq_on) ? dspu::FLT_MT_LRX_LADDERPASS : dspu::FLT_NONE;
                        }

                        fp.fGain        = p->pFreqGain[band]->value();
                        fp.nSlope       = 2;
                        fp.fQuality     = 0.0f;

                        // Update filter parameters
                        eq->set_params(band++, &fp);
                    }

                    // Setup hi-pass filter
                    fp.nType        = (low_on) ? dspu::FLT_BT_BWC_HIPASS : dspu::FLT_NONE;
                    fp.fFreq        = p->pLowFreq->value();
                    fp.fFreq2       = fp.fFreq;
                    fp.fGain        = 1.0f;
                    fp.nSlope       = 4;
                    fp.fQuality     = 0.0f;
                    eq->set_params(band++, &fp);

                    // Setup low-pass filter
                    fp.nType        = (high_on) ? dspu::FLT_BT_BWC_LOPASS : dspu::FLT_NONE;
                    fp.fFreq        = p->pHighFreq->value();
                    fp.fFreq2       = fp.fFreq;
                    fp.fGain        = 1.0f;
                    fp.nSlope       = 4;
                    fp.fQuality     = 0.0f;
                    eq->set_params(band++, &fp);
                }
            }
        }

        void slap_delay::update_sample_rate(long sr)
        {
            // Calculate maximum possible delay
            float stretch_max   = meta::slap_delay_metadata::STRETCH_MAX * 0.01f;
            float time_max      = meta::slap_delay_metadata::TIME_MAX;
            float dist_max      = meta::slap_delay_metadata::DISTANCE_MAX / dspu::sound_speed(meta::slap_delay_metadata::TEMPERATURE_MIN);
            float tempo_max     = (240.0f * meta::slap_delay_metadata::FRACTION_MAX) / meta::slap_delay_metadata::TEMPO_MIN; // time per FRACTION_MAX whole notes

            size_t time_delay   = dspu::millis_to_samples(sr, time_max * stretch_max + meta::slap_delay_metadata::PRED_TIME_MAX);
            size_t dist_delay   = dspu::seconds_to_samples(sr, dist_max * stretch_max + meta::slap_delay_metadata::PRED_TIME_MAX * 0.001f);
            size_t tempo_delay  = dspu::seconds_to_samples(sr, tempo_max * stretch_max + meta::slap_delay_metadata::PRED_TIME_MAX * 0.001f);
            size_t max_delay    = lsp_max(time_delay, dist_delay, tempo_delay);
            size_t buf_size     = align_size(max_delay + BUFFER_SIZE, BUFFER_SIZE);

//            lsp_trace("time_delay=%d, dist_delay=%d, tempo_delay=%d, max_delay=%d, buf_size=%d",
//                int(time_delay), int(dist_delay), int(tempo_delay), int(max_delay), int(buf_size));

            // Initialize devices responsible for delay implementation
            for (size_t i=0; i<meta::slap_delay_metadata::MAX_PROCESSORS; ++i)
                for (size_t j=0; j<2; ++j)
                {
                    mono_processor_t *mp    = &vProcessors[i].vDelay[j];
                    mp->sBuffer.init(buf_size);
                    mp->sEqualizer.set_sample_rate(sr);
                }

            // Initialize output channels
            for (size_t i=0; i<2; ++i)
                vChannels[i].sBypass.init(sr);
        }

        void slap_delay::process_const_delay(
            float *dst, const float *src,
            mono_processor_t *mp,
            size_t delay, size_t samples)
        {
            const float feed    = (delay > 0) ? mp->fFeedback : 0.0f;
            float *head         = mp->sBuffer.head();
            bool clear          = mp->bClear;

            // For very short delays there is no profit from dsp functions
            if (delay < DELAY_PACKET_PROCESSING)
            {
                float *tail         = mp->sBuffer.tail(delay);
                float *begin        = mp->sBuffer.begin();
                float *end          = mp->sBuffer.end();

                for (size_t offset=0; offset < samples; ++offset)
                {
                    if ((clear) && (tail >= head))
                    {
                        *head               = src[offset];
                        dst[offset]         = 0.0f;
                    }
                    else
                    {
                        *head               = src[offset] + (*tail) * feed;
                        dst[offset]         = *tail;
                    }

                    ++head;
                    ++tail;
                    if (head >= end)
                    {
                        head                = begin;
                        clear               = false;
                    }
                    if (tail >= end)
                        tail                = begin;
                }

                mp->sBuffer.advance(samples);
                mp->bClear          = clear;

                return;
            }

            // Process large blocks
            for (size_t offset=0; offset < samples; )
            {
                const size_t to_do  = lsp_min(samples - offset, mp->sBuffer.remaining(delay), delay);
                float *tail         = mp->sBuffer.tail(delay);

                if ((clear) && (tail >= head))
                {
                    dsp::copy(head, &src[offset], to_do);
                    dsp::fill_zero(&dst[offset], to_do);
                }
                else
                {
                    dsp::fmadd_k4(head, &src[offset], tail, feed, to_do);
                    dsp::copy(&dst[offset], tail, to_do);
                }

                float *new_head     = mp->sBuffer.advance(to_do);
                if (new_head < head)
                    clear               = false;
                head                = new_head;
                offset             += to_do;
            }

            mp->bClear          = clear;
        }

        void slap_delay::process_varying_delay(
            float *dst, const float *src,
            mono_processor_t *mp,
            size_t delay, float delta,
            size_t step, size_t samples)
        {
            float *head         = mp->sBuffer.head();
            bool clear          = mp->bClear;

            for (size_t offset=0; offset < samples; ++offset)
            {
                const size_t vdelay = delay + (offset + step)*delta;
                const float feed    = (vdelay > 0) ? mp->fFeedback : 0.0f;
                float *tail         = mp->sBuffer.tail(vdelay);

                if ((clear) && (tail >= head))
                {
                    *head               = src[offset];
                    dst[offset]         = 0.0f;
                }
                else
                {
                    *head               = src[offset] + (*tail) * feed;
                    dst[offset]         = *tail;
                }

                // Update head
                float *new_head     = mp->sBuffer.advance(1);
                if (new_head < head)
                    clear               = false;
                head                = new_head;
            }

            mp->bClear          = clear;
        }

        void slap_delay::process(size_t samples)
        {
            // Prepare inputs and outputs
            for (size_t i=0; i<nInputs; ++i)
                vInputs[i].vIn      = vInputs[i].pIn->buffer<float>();
            for (size_t i=0; i<2; ++i)
                vChannels[i].vOut   = vChannels[i].pOut->buffer<float>();

            // Do processing
            for (size_t offset=0; offset < samples; )
            {
                // Process input data
                const size_t to_do  = lsp_min(samples - offset, BUFFER_SIZE);
                channel_t *lc       = &vChannels[0];
                channel_t *rc       = &vChannels[1];

                if (nInputs > 1)
                {
                    input_t *in_l       = &vInputs[0];
                    input_t *in_r       = &vInputs[1];

                    // Apply panning to the input signal and store it in the render buffer
                    dsp::mix_copy2(lc->vRender, in_l->vIn, in_r->vIn, lc->fGain[0], lc->fGain[1], to_do);
                    dsp::mix_copy2(rc->vRender, in_l->vIn, in_r->vIn, rc->fGain[0], rc->fGain[1], to_do);

                    // Do job with processors
                    for (size_t j=0; j<meta::slap_delay_metadata::MAX_PROCESSORS; ++j)
                    {
                        // Skip processor if it is turned off
                        processor_t *p          = &vProcessors[j];
                        if (p->nMode == meta::slap_delay_metadata::OP_MODE_NONE)
                            continue;

                        mono_processor_t *mpl   = &p->vDelay[0];
                        mono_processor_t *mpr   = &p->vDelay[1];

                        if (p->nNewDelay != p->nDelay)
                        {
                            const float delta   = (float(p->nNewDelay) - float(p->nDelay))/float(samples);
                            process_varying_delay(lc->vTemp, in_l->vIn, mpl, p->nDelay, delta, offset, to_do);
                            process_varying_delay(rc->vTemp, in_r->vIn, mpr, p->nDelay, delta, offset, to_do);
                        }
                        else
                        {
                            process_const_delay(lc->vTemp, in_l->vIn, mpl, p->nDelay, to_do);
                            process_const_delay(rc->vTemp, in_r->vIn, mpr, p->nDelay, to_do);
                        }

                        // Process data with equalizer
                        p->vDelay[0].sEqualizer.process(lc->vTemp, lc->vTemp, to_do);
                        p->vDelay[1].sEqualizer.process(rc->vTemp, rc->vTemp, to_do);

                        // Apply pan control
                        dsp::mix_add2(lc->vRender, lc->vTemp, rc->vTemp, mpl->fGain[0], mpl->fGain[1], to_do);
                        dsp::mix_add2(rc->vRender, lc->vTemp, rc->vTemp, mpr->fGain[0], mpr->fGain[1], to_do);
                    }

                    // Make output monophonic
                    if (bMono)
                    {
                        dsp::lr_to_mid(lc->vRender, lc->vRender, rc->vRender, to_do);
                        dsp::copy(rc->vRender, lc->vRender, to_do);
                    }

                    // Apply bypass
                    lc->sBypass.process(lc->vOut, in_l->vIn, lc->vRender, to_do);
                    rc->sBypass.process(rc->vOut, in_r->vIn, rc->vRender, to_do);
                }
                else
                {
                    input_t *in         = &vInputs[0];

                    // Apply panning to the input signal and store it in the render buffer
                    dsp::mul_k3(lc->vRender, in->vIn, lc->fGain[0], to_do);
                    dsp::mul_k3(rc->vRender, in->vIn, rc->fGain[0], to_do);

                    // Do job with processors
                    for (size_t j=0; j<meta::slap_delay_metadata::MAX_PROCESSORS; ++j)
                    {
                        // Skip processor if it is turned off
                        processor_t *p          = &vProcessors[j];
                        if (p->nMode == meta::slap_delay_metadata::OP_MODE_NONE)
                            continue;
                        mono_processor_t *mp    = &p->vDelay[0];

                        if (p->nNewDelay != p->nDelay)
                        {
                            const float delta   = (float(p->nNewDelay) - float(p->nDelay))/float(samples);
                            process_varying_delay(lc->vTemp, in->vIn, mp, p->nDelay, delta, offset, to_do);
                        }
                        else
                        {
                            process_const_delay(lc->vTemp, in->vIn, mp, p->nDelay, to_do);
                        }

                        // Process data with equalizer
                        p->vDelay[0].sEqualizer.process(lc->vTemp, lc->vTemp, to_do);

                        // Apply pan control
                        dsp::fmadd_k3(lc->vRender, lc->vTemp, mp->fGain[0], to_do);
                        dsp::fmadd_k3(rc->vRender, lc->vTemp, mp->fGain[1], to_do);
                    }

                    // Make output monophonic
                    if (bMono)
                    {
                        dsp::lr_to_mid(lc->vRender, lc->vRender, rc->vRender, to_do);
                        dsp::copy(rc->vRender, lc->vRender, to_do);
                    }

                    // Apply bypass
                    lc->sBypass.process(lc->vOut, in->vIn, lc->vRender, to_do);
                    rc->sBypass.process(rc->vOut, in->vIn, rc->vRender, to_do);
                }

                // Adjust delay
                for (size_t j=0; j<meta::slap_delay_metadata::MAX_PROCESSORS; ++j)
                    vProcessors[j].nDelay   = vProcessors[j].nNewDelay;

                // Update pointers
                for (size_t i=0; i<nInputs; ++i)
                    vInputs[i].vIn     += to_do;
                for (size_t i=0; i<2; ++i)
                    vChannels[i].vOut  += to_do;
                offset   += to_do;
            }
        }

        void slap_delay::dump(dspu::IStateDumper *v) const
        {
            plug::Module::dump(v);

            v->write("nInputs", nInputs);
            v->begin_array("vInputs", vInputs, nInputs);
            {
                for (size_t i=0; i<nInputs; ++i)
                {
                    const input_t *in = &vInputs[i];
                    v->begin_object(in, sizeof(input_t));
                    {
                        v->write("vIn", in->vIn);
                        v->write("pIn", in->pIn);
                        v->write("pPan", in->pPan);
                    }
                    v->end_object();
                }
            }
            v->end_array();
            v->begin_array("vProcessors", vProcessors, meta::slap_delay_metadata::MAX_PROCESSORS);
            {
                for (size_t i=0; i<meta::slap_delay_metadata::MAX_PROCESSORS; ++i)
                {
                    const processor_t *p = &vProcessors[i];
                    v->begin_object(p, sizeof(processor_t));
                    {
                        v->begin_array("vDelay", p->vDelay, 2);
                        {
                            for (size_t i=0; i<2; ++i)
                            {
                                const mono_processor_t *mp  = &p->vDelay[i];

                                v->write_object("sBuffer", &mp->sBuffer);
                                v->write_object("sEqualizer", &mp->sEqualizer);
                                v->writev("fGain", mp->fGain, 2);
                                v->write("fFeedback", mp->fFeedback);
                            }
                        }
                        v->end_array();

                        v->write("nDelay", p->nDelay);
                        v->write("nNewDelay", p->nNewDelay);
                        v->write("nMode", p->nMode);

                        v->write("pMode", p->pMode);
                        v->write("pEq", p->pEq);
                        v->write("pTime", p->pTime);
                        v->write("pDistance", p->pDistance);
                        v->write("pFrac", p->pFrac);
                        v->write("pDenom", p->pDenom);
                        v->writev("pPan", p->pPan, 2);
                        v->write("pFeedback", p->pFeedback);
                        v->write("pGain", p->pGain);
                        v->write("pLowCut", p->pLowCut);
                        v->write("pLowFreq", p->pLowFreq);
                        v->write("pHighCut", p->pHighCut);
                        v->write("pHighFreq", p->pHighFreq);
                        v->write("pSolo", p->pSolo);
                        v->write("pMute", p->pMute);
                        v->write("pPhase", p->pPhase);
                        v->writev("pFreqGain", p->pFreqGain, meta::slap_delay_metadata::EQ_BANDS);
                    }
                }
            }
            v->end_array();
            v->begin_array("vChannels", vChannels, 2);
            {
                for (size_t i=0; i<2; ++i)
                {
                    const channel_t *c = &vChannels[i];
                    v->begin_object(c, sizeof(channel_t));
                    {
                        v->write_object("sBypass", &c->sBypass);
                        v->writev("fGain", c->fGain, 2);
                        v->write("vRender", c->vRender);
                        v->write("vTemp", c->vTemp);
                        v->write("vOut", c->vOut);
                        v->write("pOut", c->pOut);
                    }
                    v->end_object();
                }
            }
            v->end_array();

            v->write("bMono", bMono);

            v->write("pBypass", pBypass);
            v->write("pTemp", pTemp);
            v->write("pDry", pDry);
            v->write("pDryMute", pDryMute);
            v->write("pWet", pWet);
            v->write("pWetMute", pWetMute);
            v->write("pDryWet", pDryWet);
            v->write("pOutGain", pOutGain);
            v->write("pMono", pMono);
            v->write("pPred", pPred);
            v->write("pStretch", pStretch);
            v->write("pTempo", pTempo);
            v->write("pSync", pSync);
            v->write("pRamping", pRamping);

            v->write("vData", vData);
        }

    } /* namespace plugins */
} /* namespace lsp */


