/*
 * Copyright (C) 2020 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2020 Vladimir Sadovnikov <sadko4u@gmail.com>
 *
 * This file is part of lsp-plugins-comp-delay
 * Created on: 25 нояб. 2020 г.
 *
 * lsp-plugins-comp-delay is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * lsp-plugins-comp-delay is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with lsp-plugins-comp-delay. If not, see <https://www.gnu.org/licenses/>.
 */

#include <private/plugins/comp_delay.h>
#include <lsp-plug.in/dsp-units/units.h>
#include <lsp-plug.in/common/alloc.h>
#include <lsp-plug.in/common/debug.h>
#include <lsp-plug.in/dsp/dsp.h>

#define BUFFER_SIZE         0x1000U

#define TRACE_PORT(p)       lsp_trace("  port id=%s", (p)->metadata()->id);

namespace lsp
{
    namespace plugins
    {
        //---------------------------------------------------------------------
        // Plugin factory
        static const meta::plugin_t *plugins[] =
        {
            &meta::comp_delay_mono,
            &meta::comp_delay_stereo,
            &meta::comp_delay_x2_stereo
        };

        static plug::Module *plugin_factory(const meta::plugin_t *meta)
        {
            return new comp_delay(meta);
        }

        static plug::Factory factory(plugin_factory, plugins, 3);

        //---------------------------------------------------------------------
        // Implementation
        comp_delay::comp_delay(const meta::plugin_t *meta):
            Module(meta)
        {
            // Check plugin type
            nMode           = (meta == &meta::comp_delay_mono) ? CD_MONO :
                              (meta == &meta::comp_delay_stereo) ? CD_STEREO :
                              (meta == &meta::comp_delay_x2_stereo) ? CD_X2_STEREO :
                              CD_MONO;

            // Initialize other parameters
            vChannels       = NULL;
            vBuffer         = NULL;

            pBypass         = NULL;
            pGainOut        = NULL;

            pData           = NULL;
        }

        comp_delay::~comp_delay()
        {
            destroy();
        }

        void comp_delay::init(plug::IWrapper *wrapper, plug::IPort **ports)
        {
            Module::init(wrapper, ports);

            size_t channels         = (nMode == CD_MONO) ? 1 : 2;
            size_t szof_channels    = align_size(sizeof(channel_t) * channels, OPTIMAL_ALIGN);
            size_t buf_sz           = BUFFER_SIZE * sizeof(float);
            size_t alloc            = szof_channels + buf_sz;

            // Allocate data
            uint8_t *ptr            = alloc_aligned<uint8_t>(pData, alloc, OPTIMAL_ALIGN);
            if (ptr == NULL)
                return;

            // Initialize channels
            vChannels               = reinterpret_cast<channel_t *>(ptr);
            ptr                    += szof_channels;
            vBuffer                 = reinterpret_cast<float *>(ptr);
            ptr                    += buf_sz;

            for (size_t i=0; i < channels; ++i)
            {
                channel_t *c            = &vChannels[i];

                c->sLine.construct();
                c->sBypass.construct();

                c->nDelay               = 0;
                c->nNewDelay            = 0;
                c->nMode                = meta::comp_delay::M_SAMPLES;
                c->bRamping             = false;
                c->fDry                 = 0.0f;
                c->fWet                 = 0.0f;

                c->pIn                  = NULL;
                c->pOut                 = NULL;
                c->pMode                = NULL;
                c->pRamping             = NULL;
                c->pSamples             = NULL;
                c->pMeters              = NULL;
                c->pCentimeters         = NULL;
                c->pTemperature         = NULL;
                c->pTime                = NULL;
                c->pDry                 = NULL;
                c->pWet                 = NULL;

                c->pOutTime             = NULL;
                c->pOutSamples          = NULL;
                c->pOutDistance         = NULL;
            }

            // Bind ports
            lsp_trace("Binding ports");
            size_t port_id      = 0;

            // Bind input audio ports
            for (size_t i=0; i<channels; ++i)
            {
                TRACE_PORT(ports[port_id]);
                vChannels[i].pIn    = ports[port_id++];
            }

            // Bind output audio ports
            for (size_t i=0; i<channels; ++i)
            {
                TRACE_PORT(ports[port_id]);
                vChannels[i].pOut   = ports[port_id++];
            }

            // Bind bypass
            TRACE_PORT(ports[port_id]);
            pBypass              = ports[port_id++];

            // Bind channels
            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c            = &vChannels[i];

                if ((i > 0) && (nMode == CD_STEREO))
                {
                    channel_t *pc           = &vChannels[0];

                    c->pMode                = pc->pMode;
                    c->pRamping             = pc->pRamping;
                    c->pSamples             = pc->pSamples;
                    c->pMeters              = pc->pMeters;
                    c->pCentimeters         = pc->pCentimeters;
                    c->pTemperature         = pc->pTemperature;
                    c->pTime                = pc->pTime;
                    c->pDry                 = pc->pDry;
                    c->pWet                 = pc->pWet;
                }
                else
                {
                    TRACE_PORT(ports[port_id]);
                    c->pMode                = ports[port_id++];
                    TRACE_PORT(ports[port_id]);
                    c->pRamping             = ports[port_id++];
                    TRACE_PORT(ports[port_id]);
                    c->pSamples             = ports[port_id++];
                    TRACE_PORT(ports[port_id]);
                    c->pMeters              = ports[port_id++];
                    TRACE_PORT(ports[port_id]);
                    c->pCentimeters         = ports[port_id++];
                    TRACE_PORT(ports[port_id]);
                    c->pTemperature         = ports[port_id++];
                    TRACE_PORT(ports[port_id]);
                    c->pTime                = ports[port_id++];
                    TRACE_PORT(ports[port_id]);
                    c->pDry                 = ports[port_id++];
                    TRACE_PORT(ports[port_id]);
                    c->pWet                 = ports[port_id++];
                }
            }

            // Bind output gain
            TRACE_PORT(ports[port_id]);
            pGainOut            = ports[port_id++];

            // Bind output meters
            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c            = &vChannels[i];

                if ((i > 0) && (nMode == CD_STEREO))
                {
                    channel_t *pc           = &vChannels[0];

                    c->pOutTime             = pc->pOutTime;
                    c->pOutSamples          = pc->pOutSamples;
                    c->pOutDistance         = pc->pOutDistance;
                }
                else
                {
                    TRACE_PORT(ports[port_id]);
                    c->pOutTime             = ports[port_id++];
                    TRACE_PORT(ports[port_id]);
                    c->pOutSamples          = ports[port_id++];
                    TRACE_PORT(ports[port_id]);
                    c->pOutDistance         = ports[port_id++];
                }
            }
        }

        void comp_delay::destroy()
        {
            Module::destroy();

            // Destroy channels
            if (vChannels != NULL)
            {
                size_t channels = (nMode == CD_MONO) ? 1 : 2;
                for (size_t i=0; i<channels; ++i)
                {
                    channel_t *c    = &vChannels[i];
                    c->sLine.destroy();
                }
                vChannels   = NULL;
            }

            vBuffer     = NULL;

            // Free data
            if (pData != NULL)
            {
                free_aligned(pData);
                pData       = NULL;
            }
        }

        void comp_delay::update_sample_rate(long sr)
        {
            size_t channels         = (nMode == CD_MONO) ? 1 : 2;

            size_t samples          = meta::comp_delay::SAMPLES_MAX;
            size_t time_samples     = meta::comp_delay::TIME_MAX * 0.001 * sr;
            size_t dist_samples     = (meta::comp_delay::METERS_MAX + meta::comp_delay::CENTIMETERS_MAX * 0.01) /
                                        dspu::sound_speed(meta::comp_delay::TEMPERATURE_MAX);

            samples                 = lsp_max(samples, time_samples);
            samples                 = lsp_max(samples, dist_samples);

            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c    = &vChannels[i];
                c->sLine.init(samples);
                c->sBypass.init(sr);
            }
        }

        void comp_delay::update_settings()
        {
            size_t channels         = (nMode == CD_MONO) ? 1 : 2;
            float out_gain          = pGainOut->value();
            bool bypass             = pBypass->value() >= 0.5f;

            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c            = &vChannels[i];

                c->nMode                = c->pMode->value();
                c->bRamping             = c->pRamping->value() >= 0.5f;
                c->fDry                 = c->pDry->value() * out_gain;
                c->fWet                 = c->pWet->value() * out_gain;

                float temperature       = c->pTemperature->value();
                float snd_speed         = dspu::sound_speed(temperature);

                // Compute new delay value
                if (c->nMode == meta::comp_delay::M_DISTANCE)
                {
                    float distance          = c->pMeters->value() + (c->pCentimeters->value() * 0.01f);
                    c->nNewDelay            = (fSampleRate * distance) / snd_speed;
                }
                else if (c->nMode == meta::comp_delay::M_TIME)
                    c->nNewDelay            = dspu::millis_to_samples(fSampleRate, c->pTime->value());
                else
                    c->nNewDelay            = c->pSamples->value();

                // Update delay parameter for delay line
                c->nNewDelay            = lsp_max(0, c->nNewDelay);
                if (!c->bRamping)
                    c->nDelay               = c->nNewDelay;

                // Update processors
                c->sLine.set_delay(c->nDelay);
                c->sBypass.set_bypass(bypass);

                // Re-calculate output parameters
                c->pOutSamples->set_value(c->nNewDelay);
                c->pOutDistance->set_value((c->nNewDelay * snd_speed * 100.0f) / float(fSampleRate));
                c->pOutTime->set_value(dspu::samples_to_millis(fSampleRate, c->nNewDelay));
            }
        }

        void comp_delay::process(size_t samples)
        {
            size_t channels         = (nMode == CD_MONO) ? 1 : 2;

            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c            = &vChannels[i];

                // Get input and output buffers
                const float *in         = c->pIn->buffer<float>();
                float *out              = c->pOut->buffer<float>();

                if ((in == NULL) || (out == NULL))
                    continue;

                // Process the delay channel
                for (size_t n=0; n<samples; )
                {
                    size_t count            = lsp_min(samples - n, BUFFER_SIZE);

                    // Pre-process signal (fill buffer)
                    c->sLine.process_ramping(vBuffer, in, c->fWet, c->nNewDelay, samples);
                    c->nDelay               = c->nNewDelay;

                    // Apply 'dry' control
                    if (c->fDry > 0.0f)
                        dsp::fmadd_k3(vBuffer, in, c->fDry, count);

                    c->sBypass.process(out, in, vBuffer, count);

                    // Increment pointers
                    in          +=  count;
                    out         +=  count;
                    n           +=  count;
                }
            }
        }

        void comp_delay::dump(dspu::IStateDumper *v) const
        {
            size_t channels         = (nMode == CD_MONO) ? 1 : 2;

            v->write("nMode", nMode);
            v->begin_array("vChannels", vChannels, channels);
            for (size_t i=0; i<channels; ++i)
            {
                channel_t *c            = &vChannels[i];

                v->begin_object(c, sizeof(channel_t));
                {
                    v->write_object("sLine", &c->sLine);
                    v->write_object("sBypass", &c->sBypass);

                    v->write("nDelay", c->nDelay);
                    v->write("nNewDelay", c->nNewDelay);
                    v->write("nMode", c->nMode);
                    v->write("bRamping", c->bRamping);
                    v->write("fDry", c->fDry);
                    v->write("fWet", c->fWet);

                    v->write("pIn", c->pIn);
                    v->write("pOut", c->pOut);
                    v->write("pMode", c->pMode);
                    v->write("pRamping", c->pRamping);
                    v->write("pSamples", c->pSamples);
                    v->write("pMeters", c->pMeters);
                    v->write("pCentimeters", c->pCentimeters);
                    v->write("pTemperature", c->pTemperature);
                    v->write("pTime", c->pTime);
                    v->write("pDry", c->pDry);
                    v->write("pWet", c->pWet);

                    v->write("pOutTime", c->pOutTime);
                    v->write("pOutSamples", c->pOutSamples);
                    v->write("pOutDistance", c->pOutDistance);
                }
                v->end_object();
            }
            v->end_array();

            v->write("vBuffer", vBuffer);

            v->write("pBypass", pBypass);
            v->write("pGainOut", pGainOut);

            v->write("pData", pData);
        }

    }
}


