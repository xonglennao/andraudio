/*
 * Copyright (c) 2011 Samalyse
 * Author: Olivier Guilyardi
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <dlfcn.h>
#include <libgen.h>
#include <stdio.h>
#include <math.h>
#include <android/api-level.h>

#ifdef TARGET_MSM7K
#include <msm7k/libaudio/AudioHardware.h>
#warning "Building for target msm7k"
#else
#include <hardware_legacy/AudioHardwareInterface.h>
#endif

#define NO_STATUS -1234567

using namespace android;

typedef AudioHardwareInterface * (* CreateAudioHardware) (void);

int sine(AudioStreamOut *output, float frequency, float duration) {
    int i, j;
    float maxamp;
    int format     = output->format();
    size_t bufsize = output->bufferSize();
    int nframes    = bufsize / output->frameSize();
#if __ANDROID_API__ <= 4    
    int nchannels  = output->channelCount();
#else
    int nchannels  = output->channels();
#endif
    int samplerate = output->sampleRate();
    union {
        int8_t  *i8;
        int16_t *i16;
    } buffer;

    if (format == AudioSystem::PCM_16_BIT) {
        maxamp   = INT16_MAX * 0.9;
    } else if (format == AudioSystem::PCM_8_BIT)  {
        maxamp   = INT8_MAX * 0.9;
    } else {
        fprintf(stderr, "Unsupported output format: %d", format);
        return 1;
    }

    buffer.i8 = new int8_t[bufsize];
    float value, angle = 0, inc = 2.0 * M_PI * frequency / (float) samplerate;
    while (duration > 0) {
        for (i = 0; i < nframes; i++) {
            angle += inc;
            if (angle > 2 * M_PI) {
                angle -= 2 * M_PI;
            }
            value = sinf(angle) * maxamp;
            for (j = 0; j < nchannels; j++) {
                if (format == AudioSystem::PCM_16_BIT)
                    buffer.i16[i * nchannels + j] = value;
                else
                    buffer.i8[i * nchannels + j]  = value;
            }
        }
        output->write(buffer.i8, bufsize);
        duration -= nframes / (float) samplerate;
    }

    delete[] buffer.i8;

    return 0;
}

int main(int argc, char *argv[]) {

    char *program = basename(argv[0]);
    if (argc > 2) {
        fprintf(stderr, "Error: invalid arguments\n");
        fprintf(stderr, "Usage: %s [library_filename]\n", program);
        return 1;
    }

    AudioHardwareInterface *hardware;

    printf("------------------------------------------------\n");
    if (argc > 1) {

        void *handle = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
        if (!handle) {
            fprintf(stderr, "Error: failed to dlopen %s\n", argv[1]);
            return 1;
        }
       
        CreateAudioHardware dynCreateAudioHardware = (CreateAudioHardware) dlsym(handle, "createAudioHardware");

        if (!handle) {
            fprintf(stderr, "Error: failed to bind symbol createAudioHardware\n");
            return 1;
        }

        printf("Loaded with dlopen: %s\n", argv[1]);
        hardware = dynCreateAudioHardware();
    } else {
        printf("Skipping dlopen\n");
        hardware = createAudioHardware();
    }
    if (!hardware) {
        fprintf(stderr, "Error: failed to create audio hardware interface instance\n");
        return 1;
    }

    // see status codes in frameworks/base/include/utils/Errors.h
    status_t status; 

    if ((status = hardware->initCheck()) != OK) {
        fprintf(stderr, "Error: failed to initialize audio hardware interface; status: 0x%x\n", status);
        return 1;
    }

    if ((status = hardware->setMode(AudioSystem::MODE_NORMAL)) != OK) {
        fprintf(stderr, "Error: failed to set mode, status: 0x%x\n", status);
        return 1;
    }

    printf("------------------------------------------------\n");
    printf("Created audio hardware interface. Dumping state: \n");
    printf("------------------------------------------------\n");
    Vector<String16> args;
    hardware->dumpState(1, args);
    printf("------------------------------------------------\n");

    status = NO_STATUS;
#if __ANDROID_API__ <= 4    
    AudioStreamOut *output = hardware->openOutputStream(0, 0, 0, &status);
#else
    AudioStreamOut *output = hardware->openOutputStream(AudioSystem::DEVICE_OUT_DEFAULT, 0, 0, 0, &status);
#endif    
    if (!output) {
        fprintf(stderr, "Error: failed to open output stream\n");
        return 1;
    }
    if (status == NO_STATUS) {
        fprintf(stderr, "Error: failed to open output stream; no status reported\n");
        return 1;
    }
    if (status != OK) {
        fprintf(stderr, "Error: failed to open output stream; status: 0x%x\n", status);
        return 1;
    }

    printf("Opened output stream. Dumping state:\n");
    printf("------------------------------------------------\n");

    output->dump(1, args);

    printf("------------------------------------------------\n");

    printf("samplerate: %d\n", output->sampleRate());
    printf("buffersize: %d\n", output->bufferSize());
#if __ANDROID_API__ <= 4    
    printf("channels: %d\n",   output->channelCount());
#else
    printf("channels: %d\n",   output->channels());
#endif
    printf("format: %d\n",     output->format());
    printf("latency: %d\n",    output->latency());

    printf("------------------------------------------------\n");
    printf("Playing 30s 440Hz sine...\n");
    printf("------------------------------------------------\n");
     
    return sine(output, 440, 30);
}
