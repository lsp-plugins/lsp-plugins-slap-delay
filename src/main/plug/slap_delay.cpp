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
        static constexpr size_t BUFFER_SIZE = 0x1000;

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

            vTemp           = NULL;
            bMono           = false;

            for (size_t i=0; i<meta::slap_delay_metadata::MAX_PROCESSORS; ++i)
            {
                processor_t *p      = &vProcessors[i];

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
            size_t alloc    = BUFFER_SIZE * 3;
            float *ptr      = alloc_aligned<float>(vData, alloc * sizeof(float), DEFAULT_ALIGN);
            if (ptr == NULL)
                return;

            // Remember pointers
            vTemp           = advance_ptr<float>(ptr, BUFFER_SIZE);

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
                    p->vDelay[j].sEqualizer.init(meta::slap_delay_metadata::EQ_BANDS + 2, 0);
                    p->vDelay[j].sEqualizer.set_mode(dspu::EQM_IIR);
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
                // Destroy shift buffers
                for (size_t i=0; i<nInputs; ++i)
                    vInputs[i].sBuffer.destroy();

                delete [] vInputs;
                vInputs = NULL;
            }

            for (size_t i=0; i<meta::slap_delay_metadata::MAX_PROCESSORS; ++i)
            {
                processor_t *c      = &vProcessors[i];
                c->vDelay[0].sEqualizer.destroy();
                c->vDelay[1].sEqualizer.destroy();
            }

            free_aligned(vData);

            vTemp       = NULL;
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

                // Calculate delay gain
                float delay_gain    = (p->pMute->value() >= 0.5f) ? 0.0f : p->pGain->value() * wet_gain;
                if ((has_solo) && (p->pSolo->value() < 0.5f))
                    delay_gain          = 0.0f;
                if (p->pPhase->value() >= 0.5f)
                    delay_gain          = -delay_gain;

                // Apply panning parameters
                if (nInputs == 1)
                {
                    float pan               = p->pPan[0]->value();
                    p->vDelay[0].fGain[0]   = ((100.0f - pan) * 0.005f) * delay_gain;
                    p->vDelay[0].fGain[1]   = 0.0f;
                    p->vDelay[1].fGain[0]   = ((100.0f + pan) * 0.005f) * delay_gain;
                    p->vDelay[1].fGain[1]   = 0.0f;
                }
                else
                {
                    float pan_l             = p->pPan[0]->value();
                    float pan_r             = p->pPan[1]->value();

                    p->vDelay[0].fGain[0]   = (100.0f - pan_l) * 0.005f * delay_gain;
                    p->vDelay[0].fGain[1]   = (100.0f - pan_r) * 0.005f * delay_gain;
                    p->vDelay[1].fGain[0]   = (100.0f + pan_l) * 0.005f * delay_gain;
                    p->vDelay[1].fGain[1]   = (100.0f + pan_r) * 0.005f * delay_gain;
                }

                // Determine mode
                bool eq_on          = p->pEq->value() >= 0.5f;
                bool low_on         = p->pLowCut->value() >= 0.5f;
                bool high_on        = p->pHighCut->value() >= 0.5f;
                dspu::equalizer_mode_t eq_mode = (eq_on || low_on || high_on) ? dspu::EQM_IIR : dspu::EQM_BYPASS;
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

            size_t max_delay    = dspu::millis_to_samples(sr, time_max * stretch_max + meta::slap_delay_metadata::PRED_TIME_MAX);
            size_t dist_delay   = dspu::seconds_to_samples(sr, dist_max * stretch_max + meta::slap_delay_metadata::PRED_TIME_MAX * 0.001f);
            size_t tempo_delay  = dspu::seconds_to_samples(sr, tempo_max * stretch_max + meta::slap_delay_metadata::PRED_TIME_MAX * 0.001f);
            lsp_trace("max_delay = %d, dist_delay=%d, tempo_delay=%d", int(max_delay), int(dist_delay), int(tempo_delay));
            if (max_delay < dist_delay)
                max_delay           = dist_delay;
            if (max_delay < tempo_delay)
                max_delay           = tempo_delay;
            lsp_trace("max_delay (final) = %d", int(max_delay));

            // Initialize buffers and fill them with zeros
            for (size_t i=0; i<nInputs; ++i)
            {
                vInputs[i].sBuffer.init(max_delay * 2, max_delay);
                vInputs[i].sBuffer.fill(0.0f);
            }

            for (size_t i=0; i<meta::slap_delay_metadata::MAX_PROCESSORS; ++i)
            {
                processor_t *p      = &vProcessors[i];
                p->vDelay[0].sEqualizer.set_sample_rate(sr);
                p->vDelay[1].sEqualizer.set_sample_rate(sr);
            }

            // Initialize output channels
            for (size_t i=0; i<2; ++i)
                vChannels[i].sBypass.init(sr);
        }

        void slap_delay::process(size_t samples)
        {
            // Prepare inputs and outputs
            for (size_t i=0; i<nInputs; ++i)
                vInputs[i].vIn      = vInputs[i].pIn->buffer<float>();
            for (size_t i=0; i<2; ++i)
                vChannels[i].vOut   = vChannels[i].pOut->buffer<float>();

            // Do processing
            for (size_t k=0; k < samples; )
            {
                // Process input data
                size_t to_do        = lsp_min(samples - k, BUFFER_SIZE);
                to_do               = vInputs[0].sBuffer.append(vInputs[0].vIn, to_do);

                if (nInputs > 1)
                    vInputs[1].sBuffer.append(vInputs[1].vIn, to_do); // Buffer has the same gap, nothing to worry about to_do

                // Process each channel individually
                for (size_t i=0; i<2; ++i)
                {
                    channel_t *c        = &vChannels[i];

                    // Copy dry data to rendering buffer
                    if (nInputs == 1)
                        dsp::mul_k3(c->vRender, vInputs[0].vIn, c->fGain[0], to_do);
                    else
                        dsp::mix_copy2(c->vRender, vInputs[0].vIn, vInputs[1].vIn, c->fGain[0], c->fGain[1], to_do);

                    // Do job with processors
                    for (size_t j=0; j<meta::slap_delay_metadata::MAX_PROCESSORS; ++j)
                    {
                        // Skip processor if it is turned off
                        processor_t *p      = &vProcessors[j];
                        if (p->nMode == meta::slap_delay_metadata::OP_MODE_NONE)
                            continue;

                        if (p->nNewDelay == p->nDelay)
                        {
                            // Copy delayed signal to buffer and apply panoraming
                            size_t delay        = p->nDelay + to_do;
                            if (nInputs == 1)
                                dsp::mul_k3(vTemp, vInputs[0].sBuffer.tail(delay), p->vDelay[i].fGain[0], to_do);
                            else
                                dsp::mix_copy2(vTemp, vInputs[0].sBuffer.tail(delay), vInputs[1].sBuffer.tail(delay), p->vDelay[i].fGain[0], p->vDelay[i].fGain[1], to_do);
                        }
                        else
                        {
                            // More complicated algorithm with ramping
                            float delta = (float(p->nNewDelay) - float(p->nDelay))/float(samples);

                            if (nInputs == 1)
                            {
                                float g0 = p->vDelay[i].fGain[0];
                                const float *s0 = vInputs[0].sBuffer.tail(to_do);

                                for (size_t n=0; n < to_do; ++n, ++s0)
                                {
                                    ssize_t d = p->nDelay + delta * (k + n);
                                    vTemp[n] = s0[-d] * g0;
                                }
                            }
                            else
                            {
                                float g0 = p->vDelay[i].fGain[0];
                                float g1 = p->vDelay[i].fGain[1];

                                const float *s0 = vInputs[0].sBuffer.tail(to_do);
                                const float *s1 = vInputs[1].sBuffer.tail(to_do);

                                for (size_t n=0; n < to_do; ++n, ++s0, ++s1)
                                {
                                    ssize_t d = p->nDelay + delta * (k + n);
                                    vTemp[n] = s0[-d] * g0 + s1[-d] * g1;
                                }
                            }
                        }

                        // Process data with equalizer
                        p->vDelay[i].sEqualizer.process(vTemp, vTemp, to_do);

                        // Alright, append temporary buffer to render buffer
                        dsp::add2(c->vRender, vTemp, to_do);
                    }
                }

                // Make output monophonic
                if (bMono)
                {
                    dsp::lr_to_mid(vChannels[0].vRender, vChannels[0].vRender, vChannels[1].vRender, to_do);
                    dsp::copy(vChannels[1].vRender, vChannels[0].vRender, to_do);
                }

                // Process each channel individually
                for (size_t i=0; i<2; ++i)
                {
                    // Apply bypass
                    channel_t *c        = &vChannels[i];
                    c->sBypass.process(c->vOut, vInputs[i%nInputs].vIn, c->vRender, to_do);
                }

                // Adjust delay
                for (size_t j=0; j<meta::slap_delay_metadata::MAX_PROCESSORS; ++j)
                    vProcessors[j].nDelay   = vProcessors[j].nNewDelay;

                // Remove rare data from shift buffers
                vInputs[0].sBuffer.shift(to_do);
                if (nInputs > 1)
                    vInputs[1].sBuffer.shift(to_do);

                // Update pointers
                for (size_t i=0; i<nInputs; ++i)
                    vInputs[i].vIn     += to_do;
                for (size_t i=0; i<2; ++i)
                    vChannels[i].vOut  += to_do;
                k   += to_do;
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
                        v->write_object("sBuffer", &in->sBuffer);
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

                                v->write_object("sEqualizer", &mp->sEqualizer);
                                v->writev("fGain", mp->fGain, 2);
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
                        v->write("pGain", p->pGain);
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
                        v->write("vOut", c->vOut);
                        v->write("pOut", c->pOut);
                    }
                    v->end_object();
                }
            }
            v->end_array();

            v->write("vTemp", vTemp);
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


