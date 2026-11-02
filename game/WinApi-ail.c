/**
 *
 *  Copyright (C) 2026 mxvisor
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy of
 *  this software and associated documentation files (the "Software"), to deal in
 *  the Software without restriction, including without limitation the rights to
 *  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 *  the Software, and to permit persons to whom the Software is furnished to do so,
 *  subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 *  FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 *  COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 *  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 *  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

/* The sound of Comix Zone: the Miles Sound System (WAIL32.dll).  The game
 * allocates sample handles of the digital driver and plays 8/16 bit PCM from
 * memory it got from MEM_alloc_lock; the port plays the samples through the
 * SDL_mixer device of the runtime.  The sequences (music) are XMIDI: the port
 * turns them into MIDI and plays them with the SDK's Game_midi (TinySoundFont,
 * soundfont.sf2 of the game directory). */

#include "WinApi-game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <SDL_mixer.h>

#include "WinApi.h"
#include "Game_midi.h"
#include "Game_sound.h"
#include "Game_vfs.h"


extern uint64_t pointer_offset;
#define GUEST_MEM(a) ((void *)(uintptr_t)((a) ? ((uint64_t)(a) + pointer_offset) : 0))

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned int Game_AudioFormat;
extern unsigned int Game_AudioChannels;
extern int Game_AudioRate;

extern uint32_t CCALL GlobalAlloc_c(uint32_t flags, uint32_t size);
extern uint32_t CCALL HeapFree_c(uint32_t heap, uint32_t flags, uint32_t address);

/* the states of a sample and of a sequence (Miles) */
#define AIL_FREE     1
#define AIL_DONE     2
#define AIL_PLAYING  4
#define AIL_STOPPED  8

#define AIL_HANDLE_BASE 0x41000000u
#define AIL_MAX_OBJECTS 64

typedef struct {
    int used;
    int sequence;           /* 0: a sample, 1: a sequence */
    int status;
    uint32_t address;       /* the guest's address of the data */
    uint32_t length;
    int format, flags;
    Mix_Chunk *chunk;
    Uint8 *converted;
    int channel;
    uint32_t number;        /* the sequence of the XMIDI data */
} ail_object;

static ail_object ail_objects[AIL_MAX_OBJECTS];
static int ail_rate = 22050;
static int ail_log = -1;

#define AIL_LOG(...) do { if (ail_log < 0) ail_log = (getenv("CZ_AIL_LOG") != NULL); \
                          if (ail_log) fprintf(stderr, "ail: " __VA_ARGS__); } while (0)

static uint32_t ail_new(int sequence)
{
    int i;

    for (i = 0; i < AIL_MAX_OBJECTS; i++)
    {
        if (!ail_objects[i].used)
        {
            memset(&ail_objects[i], 0, sizeof(ail_objects[i]));
            ail_objects[i].used = 1;
            ail_objects[i].sequence = sequence;
            ail_objects[i].status = AIL_DONE;    /* the game looks for a handle in this state */
            ail_objects[i].channel = -1;
            return AIL_HANDLE_BASE + (uint32_t)i * 0x100;
        }
    }

    return 0;
}

static ail_object *ail_get(uint32_t handle)
{
    uint32_t i;

    if (handle < AIL_HANDLE_BASE || ((handle - AIL_HANDLE_BASE) & 0xff)) return NULL;
    i = (handle - AIL_HANDLE_BASE) >> 8;
    if (i >= AIL_MAX_OBJECTS || !ail_objects[i].used) return NULL;
    return &ail_objects[i];
}

static void ail_release_chunk(ail_object *o)
{
    if (o->channel >= 0 && Mix_Playing(o->channel) && Mix_GetChunk(o->channel) == o->chunk)
    {
        Mix_HaltChannel(o->channel);
    }
    o->channel = -1;
    if (o->chunk != NULL)
    {
        Mix_FreeChunk(o->chunk);
        o->chunk = NULL;
    }
    if (o->converted != NULL)
    {
        SDL_free(o->converted);
        o->converted = NULL;
    }
}

/* the sample plays while its channel still holds its chunk */
static int ail_sample_playing(ail_object *o)
{
    return o->chunk != NULL && o->channel >= 0 && Mix_Playing(o->channel) && Mix_GetChunk(o->channel) == o->chunk;
}

static void ail_play_sample(ail_object *o)
{
    SDL_AudioCVT cvt;
    SDL_AudioFormat src;
    int channels, bytes;
    uint8_t *data;
    Uint8 *buf;
    Uint32 length;

    ail_release_chunk(o);

    if (!Game_AudioIsOpen() || o->address == 0 || o->length == 0)
    {
        o->status = AIL_DONE;
        return;
    }

    switch (o->format & 3)
    {
        case 0: src = (o->flags & 1) ? AUDIO_S8 : AUDIO_U8; channels = 1; bytes = 1; break;
        case 1: src = (o->flags & 1) ? AUDIO_S8 : AUDIO_U8; channels = 2; bytes = 1; break;
        case 2: src = AUDIO_S16LSB; channels = 1; bytes = 2; break;
        default: src = AUDIO_S16LSB; channels = 2; bytes = 2; break;
    }
    (void)bytes;

    if (SDL_BuildAudioCVT(&cvt, src, (Uint8)channels, ail_rate,
                          (SDL_AudioFormat)Game_AudioFormat, (Uint8)Game_AudioChannels, Game_AudioRate) < 0)
    {
        o->status = AIL_DONE;
        return;
    }

    data = (uint8_t *)GUEST_MEM(o->address);
    length = o->length;
    buf = (Uint8 *)SDL_malloc((size_t)length * (size_t)(cvt.len_mult > 0 ? cvt.len_mult : 1));
    if (buf == NULL)
    {
        o->status = AIL_DONE;
        return;
    }
    memcpy(buf, data, length);
    cvt.buf = buf;
    cvt.len = (int)length;
    if (SDL_ConvertAudio(&cvt) < 0)
    {
        SDL_free(buf);
        o->status = AIL_DONE;
        return;
    }

    o->converted = buf;
    o->chunk = Mix_QuickLoad_RAW(buf, (Uint32)cvt.len_cvt);
    if (o->chunk == NULL)
    {
        SDL_free(buf);
        o->converted = NULL;
        o->status = AIL_DONE;
        return;
    }

    o->channel = Mix_PlayChannel(-1, o->chunk, 0);
    o->status = (o->channel >= 0) ? AIL_PLAYING : AIL_DONE;
}


/* --- the music: XMIDI to a standard MIDI file, played by SDK's Game_midi --- */

static ail_object *ail_music = NULL;      /* the sequence that plays */

static void ail_play_sequence(ail_object *o)
{
    uint8_t *midi;
    size_t length = 0;
    int infinite = 0;
    uint32_t size;

    if (!Game_AudioIsOpen() || o->address == 0) { o->status = AIL_DONE; return; }

    Game_MidiStop();
    ail_music = NULL;

    size = ((uint32_t *)GUEST_MEM(o->address))[-1];     /* the size is in the header of the guest's block */
    midi = Game_MidiFromXmidi((const uint8_t *)GUEST_MEM(o->address), size, o->number, &length, &infinite);
    if (midi == NULL)
    {
        AIL_LOG("sequence %x: no XMIDI\n", (unsigned)o->address);
        o->status = AIL_DONE;
        return;
    }

    if (!Game_MidiPlay(midi, length, infinite))
    {
        free(midi);
        o->status = AIL_DONE;
        return;
    }

    free(midi);                 /* TML copied the messages */
    ail_music = o;
    o->status = AIL_PLAYING;
}

uint32_t CCALL AIL_startup_c(void)
{
    AIL_LOG("startup\n");
    return 1;
}

uint32_t CCALL AIL_shutdown_c(void)
{
    int i;

    AIL_LOG("shutdown\n");
    Game_MidiShutdown();
    ail_music = NULL;
    for (i = 0; i < AIL_MAX_OBJECTS; i++)
    {
        if (ail_objects[i].used) ail_release_chunk(&ail_objects[i]);
        ail_objects[i].used = 0;
    }
    return 0;
}

/* HDIGDRIVER *driver, ..., format: the driver's rate is the one of the WAVEFORMAT */
uint32_t CCALL AIL_waveOutOpen_c(uint32_t driver, uint32_t waveout, uint32_t device, uint32_t format)
{
    uint32_t *out;
    const uint8_t *fmt = (const uint8_t *)GUEST_MEM(format);

    if (fmt != NULL)
    {
        uint32_t rate;
        memcpy(&rate, fmt + 4, 4);
        if (rate >= 4000 && rate <= 48000) ail_rate = (int)rate;
        AIL_LOG("waveOutOpen: tag %u channels %u rate %u bits %u\n",
                (unsigned)(fmt[0] | (fmt[1] << 8)), (unsigned)(fmt[2] | (fmt[3] << 8)), (unsigned)rate,
                (unsigned)(fmt[14] | (fmt[15] << 8)));
    }

    out = (uint32_t *)GUEST_MEM(driver);
    if (out != NULL) *out = AIL_HANDLE_BASE - 0x100;
    (void)waveout; (void)device;
    return 0;
}

/* HMDIDRIVER *driver, ... */
uint32_t CCALL AIL_midiOutOpen_c(uint32_t driver, uint32_t midiout, uint32_t device)
{
    uint32_t *out = (uint32_t *)GUEST_MEM(driver);

    AIL_LOG("midiOutOpen(device %x)\n", (unsigned)device);
    if (out != NULL) *out = AIL_HANDLE_BASE - 0x200;
    (void)midiout;
    return 0;
}

uint32_t CCALL AIL_allocate_sample_handle_c(uint32_t driver)
{
    uint32_t h = ail_new(0);

    (void)driver;
    AIL_LOG("allocate_sample_handle -> %x\n", (unsigned)h);
    return h;
}

uint32_t CCALL AIL_allocate_sequence_handle_c(uint32_t driver)
{
    uint32_t h = ail_new(1);

    (void)driver;
    AIL_LOG("allocate_sequence_handle -> %x\n", (unsigned)h);
    return h;
}

uint32_t CCALL AIL_init_sample_c(uint32_t sample)
{
    ail_object *o = ail_get(sample);

    AIL_LOG("init_sample %x\n", (unsigned)sample);
    if (o != NULL)
    {
        ail_release_chunk(o);
        o->status = AIL_DONE;
        o->address = 0;
        o->length = 0;
    }
    return 0;
}

uint32_t CCALL AIL_set_sample_type_c(uint32_t sample, uint32_t format, uint32_t flags)
{
    ail_object *o = ail_get(sample);

    AIL_LOG("set_sample_type %x format %u flags %u\n", (unsigned)sample, (unsigned)format, (unsigned)flags);
    if (o != NULL)
    {
        o->format = (int)format;
        o->flags = (int)flags;
    }
    return 0;
}

uint32_t CCALL AIL_set_sample_address_c(uint32_t sample, uint32_t start, uint32_t length)
{
    ail_object *o = ail_get(sample);

    AIL_LOG("set_sample_address %x start %x length %u\n", (unsigned)sample, (unsigned)start, (unsigned)length);
    if (o != NULL)
    {
        o->address = start;
        o->length = length;
    }
    return 0;
}

uint32_t CCALL AIL_start_sample_c(uint32_t sample)
{
    ail_object *o = ail_get(sample);

    AIL_LOG("start_sample %x\n", (unsigned)sample);
    if (o != NULL) ail_play_sample(o);
    return 0;
}

uint32_t CCALL AIL_stop_sample_c(uint32_t sample)
{
    ail_object *o = ail_get(sample);

    if (o != NULL && ail_sample_playing(o))
    {
        Mix_Pause(o->channel);
        o->status = AIL_STOPPED;
    }
    return 0;
}

uint32_t CCALL AIL_resume_sample_c(uint32_t sample)
{
    ail_object *o = ail_get(sample);

    if (o != NULL && o->status == AIL_STOPPED && o->channel >= 0)
    {
        Mix_Resume(o->channel);
        o->status = AIL_PLAYING;
    }
    return 0;
}

uint32_t CCALL AIL_end_sample_c(uint32_t sample)
{
    ail_object *o = ail_get(sample);

    if (o != NULL)
    {
        ail_release_chunk(o);
        o->status = AIL_DONE;
    }
    return 0;
}

uint32_t CCALL AIL_sample_status_c(uint32_t sample)
{
    ail_object *o = ail_get(sample);

    if (o == NULL) return AIL_FREE;
    if (o->status == AIL_PLAYING && !ail_sample_playing(o)) o->status = AIL_DONE;
    return (uint32_t)o->status;
}

uint32_t CCALL AIL_serve_c(void)
{
    return 0;
}

/* the sequences (music) are state only */
uint32_t CCALL AIL_init_sequence_c(uint32_t sequence, uint32_t start, uint32_t number)
{
    ail_object *o = ail_get(sequence);

    AIL_LOG("init_sequence %x start %x number %u\n", (unsigned)sequence, (unsigned)start, (unsigned)number);
    if (o != NULL)
    {
        o->address = start;
        o->number = number;
        o->status = AIL_STOPPED;
    }
    return 1;
}

uint32_t CCALL AIL_start_sequence_c(uint32_t sequence)
{
    ail_object *o = ail_get(sequence);

    AIL_LOG("start_sequence %x\n", (unsigned)sequence);
    if (o != NULL) ail_play_sequence(o);
    return 0;
}

uint32_t CCALL AIL_stop_sequence_c(uint32_t sequence)
{
    ail_object *o = ail_get(sequence);

    if (o != NULL && o->status == AIL_PLAYING)
    {
        if (o == ail_music) Game_MidiPause(1);
        o->status = AIL_STOPPED;
    }
    return 0;
}

uint32_t CCALL AIL_resume_sequence_c(uint32_t sequence)
{
    ail_object *o = ail_get(sequence);

    if (o != NULL && o->status == AIL_STOPPED)
    {
        if (o == ail_music) Game_MidiPause(0);
        o->status = AIL_PLAYING;
    }
    return 0;
}

uint32_t CCALL AIL_end_sequence_c(uint32_t sequence)
{
    ail_object *o = ail_get(sequence);

    AIL_LOG("end_sequence %x\n", (unsigned)sequence);
    if (o != NULL)
    {
        if (o == ail_music)
        {
            Game_MidiStop();
            ail_music = NULL;
        }
        o->status = AIL_DONE;
    }
    return 0;
}

uint32_t CCALL AIL_sequence_status_c(uint32_t sequence)
{
    ail_object *o = ail_get(sequence);

    if (o == NULL) return AIL_FREE;
    if (o->status == AIL_PLAYING && o == ail_music && Game_MidiFinished()) o->status = AIL_DONE;
    return (uint32_t)o->status;
}

/* the memory the sounds are copied into: guest memory */
uint32_t CCALL MEM_alloc_lock_c(uint32_t size)
{
    uint32_t p = GlobalAlloc_c(0x40, size);

    AIL_LOG("MEM_alloc_lock %u -> %x\n", (unsigned)size, (unsigned)p);
    return p;
}

uint32_t CCALL MEM_free_lock_c(uint32_t address)
{
    if (address != 0) HeapFree_c(0, 0, address);
    return 0;
}

uint32_t CCALL AIL_copy_to_MEM16_c(uint32_t destination, uint32_t source, uint32_t size)
{
    void *dst = GUEST_MEM(destination), *src = GUEST_MEM(source);

    AIL_LOG("copy_to_MEM16 %x <- %x, %u bytes\n", (unsigned)destination, (unsigned)source, (unsigned)size);
    if (dst != NULL && src != NULL) memmove(dst, src, size);
    return 0;
}

#ifdef __cplusplus
}
#endif
