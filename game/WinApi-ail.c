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
 * turns them into MIDI and renders them with TinySoundFont from the
 * soundfont.sf2 of the game directory. */

#include "WinApi-game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <SDL_mixer.h>

#include "WinApi.h"
#include "Game_sound.h"
#include "Game_vfs.h"

#include "tsf.h"
#include "tml.h"

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


/* --- the music: XMIDI to a standard MIDI file, played by SDL_mixer --- */

typedef struct {
    uint8_t *data;
    size_t length, capacity;
} ail_buffer;

static void ail_put(ail_buffer *b, const void *data, size_t n)
{
    if (b->length + n > b->capacity)
    {
        size_t cap = b->capacity ? b->capacity * 2 : 4096;
        while (cap < b->length + n) cap *= 2;
        b->data = (uint8_t *)realloc(b->data, cap);
        b->capacity = cap;
    }
    memcpy(b->data + b->length, data, n);
    b->length += n;
}

static void ail_put_byte(ail_buffer *b, int v)
{
    uint8_t c = (uint8_t)v;
    ail_put(b, &c, 1);
}

static void ail_put_vlq(ail_buffer *b, uint32_t v)
{
    uint8_t tmp[5];
    int n = 0;

    tmp[n++] = v & 0x7f;
    while ((v >>= 7) != 0) tmp[n++] = (uint8_t)((v & 0x7f) | 0x80);
    while (n > 0) ail_put_byte(b, tmp[--n]);
}

typedef struct { uint32_t time; uint8_t channel, note; } ail_noteoff;

#define AIL_MAX_NOTEOFF 256

/* The events of one XMIDI sequence (an EVNT chunk) to one MIDI track.  The time
 * of XMIDI is 120 ticks a second: the file has 60 ticks a quarter note and
 * 120 quarter notes a minute.  *infinite is set when the sequence loops for ever. */
static void ail_xmidi_events(const uint8_t *ev, uint32_t size, ail_buffer *track, int *infinite)
{
    ail_noteoff off[AIL_MAX_NOTEOFF];
    int noff = 0, i;
    uint32_t pos = 0, now = 0, last = 0;

    while (pos < size)
    {
        uint32_t delay = 0, due;
        int status;

        while (pos < size && ev[pos] < 0x80) delay += ev[pos++];
        due = now + delay;

        /* the notes that end before the next event */
        for (;;)
        {
            int best = -1;

            for (i = 0; i < noff; i++)
            {
                if (off[i].time <= due && (best < 0 || off[i].time < off[best].time)) best = i;
            }
            if (best < 0) break;
            ail_put_vlq(track, off[best].time - last);
            last = off[best].time;
            ail_put_byte(track, 0x80 | off[best].channel);
            ail_put_byte(track, off[best].note);
            ail_put_byte(track, 0);
            off[best] = off[--noff];
        }

        now = due;
        if (pos >= size) break;
        status = ev[pos++];

        if ((status & 0xf0) == 0x90)
        {
            uint32_t length = 0;
            int note, velocity;

            if (pos + 2 > size) break;
            note = ev[pos++];
            velocity = ev[pos++];
            while (pos < size)
            {
                int c = ev[pos++];
                length = (length << 7) | (uint32_t)(c & 0x7f);
                if (!(c & 0x80)) break;
            }
            ail_put_vlq(track, now - last);
            last = now;
            ail_put_byte(track, status);
            ail_put_byte(track, note);
            ail_put_byte(track, velocity);
            if (noff < AIL_MAX_NOTEOFF)
            {
                off[noff].time = now + length;
                off[noff].channel = (uint8_t)(status & 15);
                off[noff].note = (uint8_t)note;
                noff++;
            }
        }
        else if (status == 0xff)
        {
            uint32_t length = 0;
            int type;

            if (pos >= size) break;
            type = ev[pos++];
            while (pos < size)
            {
                int c = ev[pos++];
                length = (length << 7) | (uint32_t)(c & 0x7f);
                if (!(c & 0x80)) break;
            }
            if (type == 0x2f) break;
            /* the tempo is the one of the header; the text is dropped */
            pos += length;
        }
        else if (status == 0xf0 || status == 0xf7)
        {
            uint32_t length = 0;

            while (pos < size)
            {
                int c = ev[pos++];
                length = (length << 7) | (uint32_t)(c & 0x7f);
                if (!(c & 0x80)) break;
            }
            ail_put_vlq(track, now - last);
            last = now;
            ail_put_byte(track, status);
            ail_put_vlq(track, length);
            if (pos + length <= size) ail_put(track, ev + pos, length);
            pos += length;
        }
        else if (status >= 0x80)
        {
            int count = ((status & 0xf0) == 0xc0 || (status & 0xf0) == 0xd0) ? 1 : 2;
            int a, b = 0;

            if (pos + count > size) break;
            a = ev[pos++];
            if (count == 2) b = ev[pos++];

            if ((status & 0xf0) == 0xb0 && (a == 116 || a == 117 || a == 118 || a == 119))
            {
                /* the loop controllers of XMIDI: a loop with the count 0 is endless */
                if (a == 116 && b == 0) *infinite = 1;
                continue;
            }
            ail_put_vlq(track, now - last);
            last = now;
            ail_put_byte(track, status);
            ail_put_byte(track, a);
            if (count == 2) ail_put_byte(track, b);
        }
    }

    /* the notes still sounding end with the sequence */
    for (i = 0; i < noff; i++)
    {
        uint32_t t = off[i].time > last ? off[i].time : last;

        ail_put_vlq(track, t - last);
        last = t;
        ail_put_byte(track, 0x80 | off[i].channel);
        ail_put_byte(track, off[i].note);
        ail_put_byte(track, 0);
    }
    ail_put_vlq(track, 0);
    ail_put_byte(track, 0xff);
    ail_put_byte(track, 0x2f);
    ail_put_byte(track, 0);
}

static uint32_t ail_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* XMIDI data (FORM XDIR, CAT XMID with FORM XMID sequences) to a MIDI file in memory */
static uint8_t *ail_xmidi_to_midi(const uint8_t *xmi, uint32_t size, uint32_t number, size_t *length, int *infinite)
{
    ail_buffer track = {NULL, 0, 0}, file = {NULL, 0, 0};
    uint32_t pos = 0, index = 0;
    static const uint8_t tempo[] = {0x00, 0xff, 0x51, 0x03, 0x07, 0xa1, 0x20};
    static const uint8_t header[] = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 60};
    uint8_t len4[4];

    *infinite = 0;

    /* find the number-th "FORM....XMID" and its EVNT chunk */
    while (pos + 12 <= size)
    {
        uint32_t chunk = ail_be32(xmi + pos + 4);

        if (memcmp(xmi + pos, "FORM", 4) == 0 && memcmp(xmi + pos + 8, "XMID", 4) == 0)
        {
            if (index++ == number)
            {
                uint32_t p = pos + 12, end = pos + 8 + chunk;

                if (end > size) end = size;
                while (p + 8 <= end)
                {
                    uint32_t clen = ail_be32(xmi + p + 4);

                    if (memcmp(xmi + p, "EVNT", 4) == 0)
                    {
                        if (p + 8 + clen > end) clen = end - p - 8;
                        ail_put(&track, tempo, sizeof(tempo));
                        ail_xmidi_events(xmi + p + 8, clen, &track, infinite);
                        goto done;
                    }
                    p += 8 + clen + (clen & 1);
                }
            }
            pos += 8 + chunk + (chunk & 1);
        }
        else if (memcmp(xmi + pos, "FORM", 4) == 0 || memcmp(xmi + pos, "CAT ", 4) == 0)
        {
            pos += 12;     /* into the container */
        }
        else
        {
            pos += 8 + chunk + (chunk & 1);
        }
    }

done:
    if (track.length == 0)
    {
        free(track.data);
        return NULL;
    }

    ail_put(&file, header, sizeof(header));
    ail_put(&file, "MTrk", 4);
    len4[0] = (uint8_t)(track.length >> 24); len4[1] = (uint8_t)(track.length >> 16);
    len4[2] = (uint8_t)(track.length >> 8); len4[3] = (uint8_t)track.length;
    ail_put(&file, len4, 4);
    ail_put(&file, track.data, track.length);
    free(track.data);
    *length = file.length;
    return file.data;
}

static ail_object *ail_music = NULL;      /* the sequence the TSF hook plays */
static tsf *ail_tsf = NULL;
static tml_message *ail_tsf_first = NULL;
static tml_message *ail_tsf_msg = NULL;
static double ail_tsf_time_ms = 0.0;
static int ail_tsf_loop = 0;
static SDL_atomic_t ail_tsf_paused;
static SDL_atomic_t ail_tsf_finished;
static char ail_soundfont_path_buf[1024];

static void ail_tsf_dispatch(double until_ms)
{
    while (ail_tsf_msg != NULL && (double)ail_tsf_msg->time <= until_ms)
    {
        tml_message *m = ail_tsf_msg;
        int ch = m->channel;

        switch (m->type)
        {
        case TML_NOTE_ON:
            if (m->velocity > 0)
                tsf_channel_note_on(ail_tsf, ch, m->key, m->velocity / 127.0f);
            else
                tsf_channel_note_off(ail_tsf, ch, m->key);
            break;
        case TML_NOTE_OFF:
            tsf_channel_note_off(ail_tsf, ch, m->key);
            break;
        case TML_PROGRAM_CHANGE:
            tsf_channel_set_presetnumber(ail_tsf, ch, m->program, ch == 9);
            break;
        case TML_CONTROL_CHANGE:
            tsf_channel_midi_control(ail_tsf, ch, m->control, m->control_value);
            break;
        case TML_PITCH_BEND:
            tsf_channel_set_pitchwheel(ail_tsf, ch, m->pitch_bend);
            break;
        default:
            break;
        }

        ail_tsf_msg = m->next;
    }
}

static void ail_tsf_mix(void *udata, Uint8 *stream, int len)
{
    int frames;
    double block_ms;

    (void)udata;
    if (ail_tsf == NULL || SDL_AtomicGet(&ail_tsf_paused)) return;

    frames = len / (int)(sizeof(short) * Game_AudioChannels);
    block_ms = frames * 1000.0 / Game_AudioRate;

    ail_tsf_dispatch(ail_tsf_time_ms);

    if (ail_tsf_msg == NULL)
    {
        if (ail_tsf_loop)
        {
            int ch;

            for (ch = 0; ch < 16; ch++) tsf_channel_sounds_off_all(ail_tsf, ch);
            ail_tsf_msg = ail_tsf_first;
            ail_tsf_time_ms = 0.0;
            ail_tsf_dispatch(0.0);
        }
        else
        {
            SDL_AtomicSet(&ail_tsf_finished, 1);
        }
    }

    tsf_render_short(ail_tsf, (short *)stream, frames, 0);
    ail_tsf_time_ms += block_ms;
}

static void ail_tsf_stop(void)
{
    Mix_HookMusic(NULL, NULL);
    if (ail_tsf != NULL)
    {
        int ch;

        for (ch = 0; ch < 16; ch++) tsf_channel_sounds_off_all(ail_tsf, ch);
    }
    tml_free(ail_tsf_first);
    ail_tsf_first = NULL;
    ail_tsf_msg = NULL;
    SDL_AtomicSet(&ail_tsf_paused, 0);
    SDL_AtomicSet(&ail_tsf_finished, 0);
    ail_music = NULL;
}

static const char *ail_soundfont_path(void)
{
    const char *root = Game_DataRoot();
    FILE *f;

    snprintf(ail_soundfont_path_buf, sizeof(ail_soundfont_path_buf),
             "%s/soundfont.sf2", root ? root : ".");
    f = fopen(ail_soundfont_path_buf, "rb");
    if (f == NULL) return NULL;
    fclose(f);
    return ail_soundfont_path_buf;
}

static int ail_tsf_load(void)
{
    const char *path = ail_soundfont_path();
    int ch;

    if (path == NULL) { AIL_LOG("no soundfont.sf2\n"); return 0; }
    ail_tsf = tsf_load_filename(path);
    if (ail_tsf == NULL) { AIL_LOG("cannot load %s\n", path); return 0; }

    tsf_set_output(ail_tsf,
                   Game_AudioChannels == 2 ? TSF_STEREO_INTERLEAVED : TSF_MONO,
                   Game_AudioRate, 0.0f);
    if (!tsf_set_max_voices(ail_tsf, 64)) AIL_LOG("tsf_set_max_voices failed\n");

    for (ch = 0; ch < 16; ch++)
        tsf_channel_set_presetnumber(ail_tsf, ch, 0, ch == 9);

    tsf_set_volume(ail_tsf, 0.5f);   /* as the old Mix_VolumeMusic(MIX_MAX_VOLUME / 2) */
    AIL_LOG("soundfont %s\n", path);
    return 1;
}

static int ail_tsf_start(const uint8_t *midi, size_t length, int infinite)
{
    if (ail_tsf == NULL && !ail_tsf_load()) return 0;
    ail_tsf_stop();

    ail_tsf_first = tml_load_memory(midi, (int)length);
    if (ail_tsf_first == NULL) { AIL_LOG("tml: bad MIDI\n"); return 0; }

    ail_tsf_msg = ail_tsf_first;
    ail_tsf_time_ms = 0.0;
    ail_tsf_loop = infinite;
    SDL_AtomicSet(&ail_tsf_paused, 0);
    SDL_AtomicSet(&ail_tsf_finished, 0);
    Mix_HookMusic(ail_tsf_mix, NULL);
    return 1;
}

static void ail_play_sequence(ail_object *o)
{
    uint8_t *midi;
    size_t length = 0;
    int infinite = 0;
    uint32_t size;

    if (!Game_AudioIsOpen() || o->address == 0) { o->status = AIL_DONE; return; }

    ail_tsf_stop();

    size = ((uint32_t *)GUEST_MEM(o->address))[-1];     /* the size is in the header of the guest's block */
    midi = ail_xmidi_to_midi((const uint8_t *)GUEST_MEM(o->address), size, o->number, &length, &infinite);
    if (midi == NULL)
    {
        AIL_LOG("sequence %x: no XMIDI\n", (unsigned)o->address);
        o->status = AIL_DONE;
        return;
    }

    if (!ail_tsf_start(midi, length, infinite))
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
    ail_tsf_stop();
    if (ail_tsf != NULL)
    {
        tsf_close(ail_tsf);
        ail_tsf = NULL;
    }
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
        if (o == ail_music) SDL_AtomicSet(&ail_tsf_paused, 1);
        o->status = AIL_STOPPED;
    }
    return 0;
}

uint32_t CCALL AIL_resume_sequence_c(uint32_t sequence)
{
    ail_object *o = ail_get(sequence);

    if (o != NULL && o->status == AIL_STOPPED)
    {
        if (o == ail_music) SDL_AtomicSet(&ail_tsf_paused, 0);
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
        if (o == ail_music) ail_tsf_stop();
        o->status = AIL_DONE;
    }
    return 0;
}

uint32_t CCALL AIL_sequence_status_c(uint32_t sequence)
{
    ail_object *o = ail_get(sequence);

    if (o == NULL) return AIL_FREE;
    if (o->status == AIL_PLAYING && o == ail_music && SDL_AtomicGet(&ail_tsf_finished)) o->status = AIL_DONE;
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
