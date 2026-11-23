// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michi71
//
// The two levels above a voice, as the D-50 arranges them:
//
//   Patch ─┬─ Tone (Upper) ─ voices ─ equalizer ─ chorus ─┐
//          └─ Tone (Lower) ─ voices ─ equalizer ─ chorus ─┴─ reverb ─ out
//
// A tone is what the panel edits: two partials, a structure, LFOs, and its
// own equalizer and chorus. A patch pairs two of them and adds the reverb.
// Voice allocation lives here too, because the D-50's polyphony is counted in
// partial pairs: sixteen for a single tone, eight each when both play.
#pragma once

#include <cstdint>

#include "d5_engine/d5_effects.h"
#include "d5_engine/d5_hot.h"
#include "d5_engine/d5_voice.h"

// fm1-d50: a measurement build's hooks (d50.cpp FM1_D50_PROF): D5_MARK(k) notes the time at point k of a block, D5_SKIP(bit) leaves a stage
// out (1 the reverb, 2 the chorus, 4 the EQ) to measure its share. Nothing without them.
#ifndef D5_MARK
#define D5_MARK(k) ((void)0)
#endif
#ifndef D5_SKIP
#define D5_SKIP(bit) false
#endif

namespace d5 {

struct ToneSpec {
    VoiceSpec voice{};
    EqSpec eq{};
    ChorusSpec chorus{};
    float level = 1.0f;
};

// One tone: its voices, then its two insert effects.
template <int kVoices = 8>
class Tone {
public:
    // How many of this tone's slots the key mode grants it. Voices past
    // the limit keep sounding until their release ends -- the render walks
    // every slot regardless, so a mode change never cuts a held note.
    void set_voice_limit(int n) {
        limit_ = n < 1 ? 1 : (n > kVoices ? kVoices : n);
        refill_free_list();
    }

    void configure(const ToneSpec& spec, float sample_rate) {
        spec_ = spec;
        sr_ = sample_rate;
        update_lfo_mask();
        eq_.configure(spec.eq, sample_rate);
        chorus_.configure(spec.chorus, sample_rate);
        // The tone's three LFOs are single shared instances -- the D-50's
        // 97.66-Hz tick walks one phase word per LFO per tone (IC25
        // 0x1508-0x160D), so a chord vibrates coherently and a legato note
        // joins the running wobble. They free-run from here on.
        for (int i = 0; i < 3; ++i) {
            lfo_[i].start(spec.voice.lfo[i], sample_rate, 0x9E3779B9u * (i + 1));
        }
    }

    // New parameters for a tone that is already sounding: specs replaced,
    // filter coefficients retuned, chorus settings handed over -- but no
    // delay line cleared and no LFO phase restarted. This is what an editor
    // sending a parameter every few milliseconds needs; configure() would
    // click on every one of them.
    void reconfigure(const ToneSpec& spec) {
        spec_ = spec;
        for (int i = 0; i < kVoices; ++i) voices_[i].set_partial_balance(spec.voice.balance);   // fm1-d50: the sounding notes too
        update_lfo_mask();
        eq_.retune(spec.eq, sr_);
        chorus_.set_type(spec.chorus.type);
        chorus_.set_rate(spec.chorus.rate);
        chorus_.set_depth(spec.chorus.depth);
        chorus_.set_balance(spec.chorus.balance);
    }

    // Returns false when the pool had nothing to give and the note was
    // dropped -- the caller uses that to leave everything else alone too.
    bool note_on(int note, float velocity) {
        // The slot comes off a free list, first in first out -- the D-50
        // keeps one per tone at 0xC330/0xC338, entries holding slot+1 with
        // zero for empty, and a note-on pops its head (0x2DA1 called from
        // 0x2B6F). A key-up appends its slot back immediately (0x2B8C ->
        // 0x63B9 -> 0x2DC5), before the release has finished, so the next
        // note lands on the slot that was released longest ago and cuts
        // whatever tail it still had. And when the head is zero the
        // firmware simply raises its no-voice flag and starts nothing:
        // the machine drops the new note rather than stealing a held one.
        //
        // This runs BEFORE the LFO work: the note-transition handler
        // builds its masks from the slots that actually took a note, so a
        // dropped note is invisible to it -- no phase reset, no delay
        // restart, nothing.
        const bool from_silence = !sounding();
        if (q_count_ == 0) {
            // Cannot happen while note-offs arrive in pairs -- the pool
            // and the governor's limit are the same number. If a note-off
            // was ever lost and no finger is down, take the pool back.
            bool any_key = false;
            for (int i = 0; i < kVoices; ++i) any_key = any_key || key_[i];
            if (any_key) return false;
            refill_free_list();
        }
        const int slot = free_q_[q_head_];
        q_head_ = (q_head_ + 1) % kVoices;
        --q_count_;
        freed_[slot] = false;
        shedding_[slot] = false;
        // Sync roles per the note-transition handler (EPROM 0x28FC-0x2991):
        // a tone going from silence to sounding restarts the phase and
        // delay of every LFO whose sync byte is nonzero (the 0x1655 loop
        // gates on [UP+3] != 0); mid-phrase, only LFO-1 may restart, and
        // only when its own byte is exactly 2 -- the KEY mode the panel
        // offers on LFO-1 alone (the gates at 0x2929/0x294E/0x2981 read
        // only C49C/C55C; a stray 2 on LFO-2/3 simply behaves as ON).
        if (from_silence) {
            for (int i = 0; i < 3; ++i) {
                if (spec_.voice.lfo[i].sync != 0) lfo_[i].retrigger();
            }
        } else if (spec_.voice.lfo[0].sync == 2) {
            lfo_[0].retrigger();
        }
        voices_[slot].bind_lfos(lfo_);
        voices_[slot].note_on(spec_.voice, note, velocity, sr_);
        note_[slot] = note;
        active_[slot] = true;
        key_[slot] = true;
        age_[slot] = 0;
        for (int i = 0; i < kVoices; ++i) {
            if (i != slot && active_[i]) ++age_[i];
        }
        return true;
    }

    // cut: the solo modes replace the old note rather than release it --
    // Roland's D-50 VST drops it by 67 dB within 200 ms where our envelope
    // release still held it at -21; a 20 ms fade cannot click.
    void note_off(int note, bool cut = false) {
        for (int i = 0; i < kVoices; ++i) {
            // Keyed by the KEY, not by whether the voice still sounds:
            // the firmware's key array outlives the envelope, so a
            // percussive voice that fell silent under a held finger still
            // owns its slot until the finger lifts -- and still gives it
            // back when it does.
            if (!key_[i] || note_[i] != note) continue;
            key_[i] = false;
            if (active_[i]) {
                if (cut) voices_[i].quick_release();
                else voices_[i].note_off();
            }
            // The slot rejoins the pool at key-up, not when its release
            // ends -- that is what lets a fast passage keep taking voices
            // whose tails are still audible.
            if (!freed_[i]) {
                freed_[i] = true;
                free_q_[(q_head_ + q_count_) % kVoices] = static_cast<uint8_t>(i);
                ++q_count_;
            }
        }
    }

    void set_wheel(float w) {
        for (int i = 0; i < kVoices; ++i) voices_[i].set_wheel(w);
    }

    // Mono fold takes the left side, the L/MONO jack: the chorus wet is
    // anti-phase on the right, so a plain average would silence it.
    float D5_HOT(next)() {
        float l, r;
        next_stereo(l, r);
        return l;
    }

    // The tone's stereo image comes entirely from its chorus: the voice
    // sum and the EQ are a mono chain, and the chorus's two counter-swept
    // wet reads open the field (the chip's effect stage does the same job).
    void D5_HOT(next_stereo)(float& l, float& r) {
        // The shared LFOs walk every sample, silent or not -- the tick
        // engine's loop at 0x1508 runs unconditionally, which is why a
        // sync-off LFO never waits for a key.
        step_lfos();
        float sum = 0.0f;
        for (int i = 0; i < kVoices; ++i) {
            if (!active_[i]) continue;
            sum += voices_[i].next();
            if (!voices_[i].active()) active_[i] = false;
        }
        finish_sample(sum, l, r);
    }

    // fm1-d50: the same a block at a time, so that the voices can be shared between two cores (Patch::render_block). begin_block steps
    // the LFOs through the block and keeps them per sample; render_voice adds one voice's block to acc, reading those (any core, one voice
    // on one core at a time); finish_sample is the rest of next_stereo for one sample of the voices' sum. In that order they do what
    // next_stereo does sample by sample; only the sum's rounding differs, as the voices are added in another order.
    // fm1-d50: D5_MAX_BLOCK; the FM-1 asks for 92..95 samples a block and sets 96 (less SRAM for the per-sample buffers)
#ifndef D5_MAX_BLOCK
#define D5_MAX_BLOCK 128
#endif
    static constexpr int kMaxBlock = D5_MAX_BLOCK;
    // fm1-d50: an LFO that no route of this tone listens to (depth 0 everywhere, no lever or aftertouch vibrato) cannot be heard, and
    // stepping it per sample was a share of every block. It stays at its start state, which reads as 0. The pitch route always rides LFO 1.
    void update_lfo_mask() {
        const VoiceSpec& v = spec_.voice;
        auto idx = [](const LfoRoute& r) { return r.lfo < 0 || r.lfo > 2 ? 0 : r.lfo; };
        uint8_t m = 0;
        for (int i = 0; i < 2; ++i) {
            if (v.pitch_lfo[i].depth != 0.0f) m |= 1;
            if (v.pw_lfo[i].depth != 0.0f) m |= static_cast<uint8_t>(1u << idx(v.pw_lfo[i]));
            if (v.tvf_lfo[i].depth != 0.0f) m |= static_cast<uint8_t>(1u << idx(v.tvf_lfo[i]));
            if (v.tva_lfo[i].depth != 0.0f) m |= static_cast<uint8_t>(1u << idx(v.tva_lfo[i]));
        }
        if (v.lever_amount != 0.0f || v.at_amount != 0.0f) m |= 1;
        lfo_mask_ = m;
    }
    void D5_HOT(step_lfos)() {
        if (lfo_mask_ & 1) lfo_[0].next();
        if (lfo_mask_ & 2) lfo_[1].next();
        if (lfo_mask_ & 4) lfo_[2].next();
    }
    void D5_HOT(begin_block)(int n) {
        // fm1-d50: one LFO at a time through the block (Lfo::fill), the same values as step_lfos and the readers per sample
        constexpr int kStride = static_cast<int>(sizeof(LfoSnap) / sizeof(float));
        static_assert(sizeof(LfoSnap) == 5 * sizeof(float), "LfoSnap: five floats");
        lfo_[0].fill(n, (lfo_mask_ & 1) != 0, &snap_[0].value[0], &snap_[0].gate0, &snap_[0].raw0, kStride);
        lfo_[1].fill(n, (lfo_mask_ & 2) != 0, &snap_[0].value[1], nullptr, nullptr, kStride);
        lfo_[2].fill(n, (lfo_mask_ & 4) != 0, &snap_[0].value[2], nullptr, nullptr, kStride);
    }
    D5_INL int active_voices(uint8_t* out) const {
        int n = 0;
        for (int i = 0; i < kVoices; ++i) {
            if (active_[i]) out[n++] = static_cast<uint8_t>(i);
        }
        return n;
    }
    void D5_HOT(render_voice)(int i, float* acc, int n) {
        if (!active_[i]) return;
        if (!voices_[i].render(snap_, acc, n)) active_[i] = false;
    }
    // fm1-d50: a tone that has had no voice for a quarter of a second and whose EQ and chorus have rung out (under -100 dB) costs
    // nothing until a voice sounds again: the FM-1's CPU spent a third of its time on silence. The chorus's sweep then stands still.
    static constexpr int kQuietSamples = 8000;
    void D5_HOT(finish_sample)(float sum, float& l, float& r) {
        if (sum == 0.0f && quiet_ >= kQuietSamples) { l = 0.0f; r = 0.0f; return; }
        const float e = D5_SKIP(4) ? sum : eq_.process(sum);
        if (D5_SKIP(2)) { l = e; r = e; }
        else chorus_.process(e, l, r);
        l *= spec_.level;
        r *= spec_.level;
        const float a = (l < 0.0f ? -l : l) + (r < 0.0f ? -r : r);
        if (sum == 0.0f && a < 1e-5f) { if (quiet_ < kQuietSamples) ++quiet_; }
        else quiet_ = 0;
    }

    // Stop everything at once and wipe what the effects still hold. Not a
    // performance action -- there is no panel button for it -- but the boot
    // benchmark needs it: it plays a chord to time the render, and while
    // its samples go to a scratch buffer, the chorus and reverb lines keep
    // what they were fed. Without this the instrument sings a little by
    // itself once the real output starts.
    void silence() {
        for (int i = 0; i < kVoices; ++i) {
            active_[i] = false;
            key_[i] = false;
            note_[i] = -1;
            age_[i] = 0;
        }
        refill_free_list();
        eq_.configure(spec_.eq, sr_);            // both clear their state
        chorus_.configure(spec_.chorus, sr_);
    }

    bool sounding() const {
        for (int i = 0; i < kVoices; ++i) {
            if (active_[i]) return true;
        }
        return false;
    }

    // fm1-d50, diagnostic: the slots whose key is down (the FM-1's mono test)
    int keys_down() const {
        int n = 0;
        for (int i = 0; i < kVoices; ++i) n += key_[i] ? 1 : 0;
        return n;
    }
    // Diagnostic: how many slots are actually rendering right now.
    int voices_sounding() const {
        int n = 0;
        for (int i = 0; i < kVoices; ++i) n += active_[i] ? 1 : 0;
        return n;
    }

    // CPU governor: retire the tail that has been ringing longest, i.e.
    // the oldest voice whose key is already up. Returns false when every
    // sounding voice is still under a finger -- those are never touched,
    // because cutting a note the player is holding is the one thing worse
    // than an underrun.
    bool shed_oldest_tail() {
        int victim = -1;
        for (int i = 0; i < kVoices; ++i) {
            // Skipping the ones already fading is the whole point: a voice
            // stays the oldest tail for the length of its fade, so without
            // this flag the governor re-picks it every block, restarts the
            // fade from its lower level each time, and nothing ever
            // actually stops. That was 500 sheds and no relief.
            if (!active_[i] || key_[i] || shedding_[i]) continue;
            if (victim < 0 || age_[i] > age_[victim]) victim = i;
        }
        if (victim < 0) return false;
        voices_[victim].quick_release();
        shedding_[victim] = true;
        return true;
    }
    // fm1-d50: the last resort of the FM-1's CPU guard, when no tail is left to retire and the audio would miss blocks: the same 20 ms
    // fade on the voice held longest. Its key keeps the slot until it is lifted, as a tail's does. The original never needs it.
    bool shed_oldest_held() {
        int victim = -1;
        for (int i = 0; i < kVoices; ++i) {
            if (!active_[i] || !key_[i] || shedding_[i]) continue;
            if (victim < 0 || age_[i] > age_[victim]) victim = i;
        }
        if (victim < 0) return false;
        voices_[victim].quick_release();
        shedding_[victim] = true;
        return true;
    }

    // Diagnostic handles for the host-side LFO sync test: the shared LFO's
    // phase and its delay/fade gate, like Voice::glide_offset_semitones().
    float lfo_phase(int i) const { return lfo_[(i < 0 || i > 2) ? 0 : i].phase(); }
    float lfo_gate(int i) const { return lfo_[(i < 0 || i > 2) ? 0 : i].gate(); }

    // Live edits that must not interrupt sounding notes.
    void set_level(float v) { spec_.level = v; }
    void set_chorus_balance(float b) { chorus_.set_balance(b); }
    void set_chorus_type(int t) { spec_.chorus.type = t; chorus_.set_type(t); }
    void set_chorus_rate(float r) { spec_.chorus.rate = r; chorus_.set_rate(r); }
    void set_chorus_depth(float d) { spec_.chorus.depth = d; chorus_.set_depth(d); }
    void set_eq(const EqSpec& e) { spec_.eq = e; eq_.retune(e, sr_); }
    const EqSpec& eq() const { return spec_.eq; }
    void set_master_cents(float c) { spec_.voice.master_cents = c; }
    void set_bend_semis(float st) {
        for (int i = 0; i < kVoices; ++i) voices_[i].set_bend_semis(st);
    }
    // fm1-d50: the bend of one note's voices only (sounding or in their release), for the D-50's mono mode: a bender per MIDI channel
    void set_note_bend(int note, float st) {
        for (int i = 0; i < kVoices; ++i)
            if (active_[i] && note_[i] == note) voices_[i].set_bend_semis(st);
    }
    void set_aftertouch(float a) {
        for (int i = 0; i < kVoices; ++i) voices_[i].set_aftertouch(a);
    }

    // CC65/CC5 override the patch's portamento while it plays; the mode
    // stays the patch's, so this can only quiet a tone the patch excluded,
    // never add one.
    void set_porta(bool sw, int time) {
        spec_.voice.porta_switch = sw;
        spec_.voice.porta_time = time;
        for (int i = 0; i < kVoices; ++i) voices_[i].set_porta(sw, time);
    }

private:
    ToneSpec spec_{};
    float sr_ = 32000.0f;
    Lfo lfo_[3]{};                  // the tone's shared three (see configure)
    // The free list: slot numbers waiting to be handed out, oldest
    // release first. Slots outside the current limit simply never enter.
    void refill_free_list() {
        q_head_ = 0;
        q_count_ = 0;
        for (int i = 0; i < kVoices; ++i) { freed_[i] = false; shedding_[i] = false; }
        for (int i = 0; i < limit_ && i < kVoices; ++i) {
            if (key_[i]) continue;             // a finger still owns it
            free_q_[q_count_++] = static_cast<uint8_t>(i);
            freed_[i] = true;
        }
    }

    Voice voices_[kVoices]{};
    int limit_ = kVoices;           // slots the key mode grants this tone
    uint8_t free_q_[kVoices] = {};
    int q_head_ = 0;
    int q_count_ = 0;
    bool freed_[kVoices] = {};      // slot is sitting in the free list
    bool key_[kVoices] = {};        // its key is still down
    bool shedding_[kVoices] = {};   // governor is already fading it out
    Equalizer eq_{};
    Chorus<> chorus_{};
    int note_[kVoices] = {};
    int age_[kVoices] = {};
    bool active_[kVoices] = {};
    LfoSnap snap_[kMaxBlock]{};      // fm1-d50: begin_block
    int quiet_ = 0;                  // fm1-d50: finish_sample
    uint8_t lfo_mask_ = 7;           // fm1-d50: update_lfo_mask
};

enum class KeyMode : uint8_t { kWhole = 0, kDual = 1, kSplit = 2 };

struct PatchSpec {
    ToneSpec upper{};
    ToneSpec lower{};
    KeyMode key_mode = KeyMode::kWhole;
    // The "-S" key modes (WHOL-S, DUAL-S, SEP-S) play monophonically: one
    // note at a time, the new note ends the old one's hold at once.
    bool solo = false;
    // The split solo modes (SPL-US, SPL-LS) hold one note per side only:
    // Roland's D-50 VST plays byte 5 as a split with a polyphonic Lower
    // and a monophonic Upper, byte 6 the other way round. (fm1-d50: the
    // D-50 firmware's SPL-US is byte 6 and SPL-LS byte 7: d5_patch_map.h.)
    bool solo_upper = false;
    bool solo_lower = false;
    // fm1-d50: SEP and SEP-S (pb[18] 3 and 8, the D-50 firmware's separate flag 0xFE30.0; FM-1 issue #62): note_on plays the Lower tone
    // polyphonically (the basic channel, 0x2B2F), sep_note_on the Upper (the patch's SepCH, 0x5DB7) -- one note at a time in SEP-S (solo).
    // key_mode stays kSplit (two tones, eight voices each, mixed as a split).
    bool separate = false;
    int sep_channel = 0;          // fm1-d50: pb[38], SepCH: 0 OFF, 1..16
    // fm1-d50: pb[21], Hold Mode: 0 U, 1 L, 2 UL. The tones the pedal holds in the split and separate modes (the firmware's 0x249F; the
    // whole and dual modes hold both). The FM-1's glue keeps the pedal's notes (d50.cpp), so it is the glue that reads it (issue #63).
    int hold_mode = 2;
    int split_point = 60;         // panel "Split Point", C4 by default
    float balance = 0.5f;         // panel "Tone Balance", upper to lower
    // Panel "Bender Range", pb[26], 0..12 semitones. Patch-common: the
    // firmware copies it to both tone slots at load time (EPROM 0x5D60,
    // C59A -> FE04/FE0C) and lets an RPN-0 data entry overwrite it until
    // the next load (0x4E72, clamped to 12).
    int bend_range = 2;
    // Panel "Output Mode", pb[29], stored 0..3 for modes 1..4. Mode 1 mixes
    // both tones to both outputs with the reverb on both. Modes 2, 3 and 4
    // put the Upper tone on the right output and the Lower tone on the
    // left, each folded to its L/MONO signal -- Roland's D-50 VST, recorded
    // dry with one tone at a time, leaves the other channel digitally
    // silent in all three, and its Stereo Polysynth is exactly this: two
    // detuned tones hard left and right. They differ in the reverb feed
    // (EPROM page 2, mixer rows 0xB4C5 + 6*mode): modes 1 and 2 send both
    // tones at half, modes 3 and 4 send one tone at full and are each
    // other's mirror: the VST, recorded one tone at a time with the reverb
    // at 50, hangs the tail on the Upper's side in mode 3 and leaves the
    // Lower dry, so mode 3 sends the Upper and mode 4 the Lower.
    // The patch loader copies the byte to CD99 and rebuilds the mixer
    // (0x66BB -> 0xB4DD). Bank 1: Jazz Guitar Duo, Stereo Polysynth,
    // Picked Guitar Duo, Slap Bass n Brass and Pianissimo use mode 2.
    int output_mode = 0;
    ReverbSpec reverb{};
    float volume = 1.0f;
};

class Patch {
public:
    // Live edit: everything except the reverb's geometry, which only has to
    // be rebuilt when the TYPE changes -- and rebuilding it empties the
    // lines, so it happens only then.
    void reconfigure(const PatchSpec& spec) {
        const bool mode_changed = spec.key_mode != spec_.key_mode;
        const int old_type = spec_.reverb.type;
        spec_ = spec;
        upper_.reconfigure(spec.upper);
        lower_.reconfigure(spec.lower);
        if (spec.reverb.type != old_type) reverb_.configure(spec.reverb, sr_);
        else reverb_.set_balance(spec.reverb.balance);
        if (mode_changed) apply_limits();
    }

    // Everything quiet, and the effect lines emptied (see Tone::silence).
    void silence() {
        upper_.silence();
        lower_.silence();
        clear_lag();
        pend_head_ = pend_count_ = 0;
        reverb_.configure(spec_.reverb, sr_);
        solo_note_[0] = solo_note_[1] = -1;
    }

    void configure(const PatchSpec& spec, float sample_rate) {
        spec_ = spec;
        sr_ = sample_rate;
        upper_.configure(spec.upper, sample_rate);
        lower_.configure(spec.lower, sample_rate);
        reverb_.configure(spec.reverb, sample_rate);
        clear_lag();
        apply_limits();
    }
    // fm1-d50: nothing of the old sound waits for the effects (D5_FX_LAG)
    void clear_lag() {
#if D5_FX_LAG
        for (int i = 0; i < kLagRing; ++i) lag_[0][i] = lag_[1][i] = 0.0f;
#endif
    }

    // fm1-d50: a cap on each tone's voices under what the key mode grants (the CPU guard in the FM-1's d50.cpp); 16: none. New notes past
    // it find no free slot; voices already sounding are not cut.
    void cap_voices(int cap) {
        cap_ = cap < 1 ? 1 : cap;
        apply_limits();
    }
    int voice_cap() const { return cap_; }

    // The sixteen-slot pool, cut the way the key mode cuts it: whole
    // gives all of them to the upper tone (the D-50's sixteen-note
    // polyphony), every other mode gives eight to each (its eight).
    void apply_limits() {
        const int up = spec_.key_mode == KeyMode::kWhole ? 16 : 8;
        upper_.set_voice_limit(up < cap_ ? up : cap_);
        lower_.set_voice_limit(8 < cap_ ? 8 : cap_);
    }

    void note_on(int note, float velocity) {
        // Solo modes: the D-50's -S family shares one voice; a new note
        // supersedes the held one. We release the previous note into its
        // release segment rather than cutting it -- close enough to the
        // steal that no factory patch tells them apart, and it cannot
        // click.
        bool sounded = false;
        const bool whole_solo = spec_.solo && !solo_off_;
        if (spec_.separate) {                    // fm1-d50: SEP, SEP-S: the basic channel's tone, the Lower, polyphonically (0x2B2F)
            lower_later(note, velocity, true, false);
            reverb_.note_activity();
            return;
        }
        switch (spec_.key_mode) {
            case KeyMode::kDual: {
                if (whole_solo) solo_cut(0, note);
                const bool u = upper_.note_on(note, velocity);
                if (whole_solo) solo_note_[0] = note;
                lower_later(note, velocity, true, whole_solo);
                sounded = u || true;
                break;
            }
            case KeyMode::kSplit: {
                const int side = note >= spec_.split_point ? 0 : 1;
                const bool solo = !solo_off_ && (side == 0 ? spec_.solo_upper : spec_.solo_lower);
                if (side == 0) {
                    if (solo) solo_cut(0, note);
                    sounded = upper_.note_on(note, velocity);
                    if (solo) solo_note_[0] = note;
                } else {
                    lower_later(note, velocity, true, solo);
                    sounded = true;
                }
                break;
            }
            case KeyMode::kWhole:
            default:
                if (whole_solo) solo_cut(0, note);
                sounded = upper_.note_on(note, velocity);
                if (whole_solo) solo_note_[0] = note;
                break;
        }
        // The gate and reverse reverbs time their wet envelope against the
        // note; a note the pool refused never reached the chip, so it must
        // not re-arm them either.
        if (sounded) reverb_.note_activity();
    }

    void note_off(int note) {
        if (!spec_.separate) {                   // (separate: the Upper is SepCH's, sep_note_off)
            upper_.note_off(note);
            if (note == solo_note_[0]) solo_note_[0] = -1;
        }
        lower_later(note, 0.0f, false, false);
    }

    // fm1-d50: the patch's SepCH in SEP and SEP-S (FM-1 issue #62, the firmware's 0x5DB7 and 0x29A7): the Upper, polyphonically in SEP
    // (0x2B29), one note at a time in SEP-S (0x2B40, a fixed voice). A received Poly does not change it (the firmware's SepCH path has no
    // rows). Outside the separate modes nothing.
    void sep_note_on(int note, float velocity) {
        if (!spec_.separate) return;
        if (spec_.solo) solo_cut(0, note);
        if (upper_.note_on(note, velocity)) reverb_.note_activity();
        if (spec_.solo) solo_note_[0] = note;
    }
    void sep_note_off(int note) {
        upper_.note_off(note);
        if (note == solo_note_[0]) solo_note_[0] = -1;
    }
    bool separate() const { return spec_.separate; }
    int sep_channel() const { return spec_.separate ? spec_.sep_channel : 0; }

    // fm1-d50: one tone's note on or off (0 Upper, 1 Lower), for the FM-1's Chase Play, which plays the tones apart (d50.cpp)
    void tone_note_on(int tone, int note, float velocity) {
        if (tone ? lower_.note_on(note, velocity) : upper_.note_on(note, velocity)) reverb_.note_activity();
    }
    void tone_note_off(int tone, int note) {
        if (tone) lower_.note_off(note);
        else upper_.note_off(note);
    }
    KeyMode key_mode() const { return spec_.key_mode; }

    // Mono fold is the L/MONO jack again: the left side as it ships. In the
    // split output modes the jack would carry only the Lower tone, so there
    // the fold sums both sides -- each tone is mono on its own side, nothing
    // anti-phase can cancel.
    float D5_HOT(next)() {
        float l, r;
        next_stereo(l, r);
        return (spec_.key_mode == KeyMode::kWhole || spec_.output_mode == 0) ? l : 0.5f * (l + r);
    }

    // Stereo: the tones keep their own left and right through the balance
    // weights into the reverb, whose two coprime networks take one side
    // each -- the chorus width of a tone survives into the room. The laws
    // themselves are unchanged from the mono path.
    void D5_HOT(next_stereo)(float& l, float& r) {
        ++clock_;
        if (pend_count_ != 0 && pending_[pend_head_].due == clock_) service_lower();
        float ul, ur, ll, lr;
        upper_.next_stereo(ul, ur);
        lower_.next_stereo(ll, lr);
        mix(ul, ur, ll, lr, l, r);
    }

    // fm1-d50: how a block's voices reach another core. begin hands it a job (false: the job runs on this core instead), wait returns
    // once the job is done. Without one, or with null functions, render_block renders every voice here.
    struct Split {
        bool (*begin)(void (*job)(void*), void* arg) = nullptr;
        void (*wait)() = nullptr;
        int share16 = 8;            // the other core's share of the sounding voices, in sixteenths (the caller may steer it)
    };

    // fm1-d50: n samples (up to Tone::kMaxBlock) into l and r, as n calls of next_stereo would make them, with every second sounding voice
    // on the other core. The block is cut where a delayed lower note is due, as next_stereo services it before that sample.
    void D5_HOT_GATE(render_block)(float* l, float* r, int n, const Split* split) {
        const uint32_t c0 = clock_;
        int s = 0;
        while (s < n) {
            if (pend_count_ != 0 && pending_[pend_head_].due == c0 + static_cast<uint32_t>(s) + 1U) {
                clock_ = c0 + static_cast<uint32_t>(s) + 1U;
                service_lower();
            }
            int m = n - s;
            if (pend_count_ != 0) {
                const uint32_t d = pending_[pend_head_].due - (c0 + static_cast<uint32_t>(s) + 1U);
                if (d >= 1U && d < static_cast<uint32_t>(m)) m = static_cast<int>(d);
            }
#if D5_GATED
            D5_FAR(part_entry)(this, l + s, r + s, m, split);
#else
            render_part(l + s, r + s, m, split);
#endif
            s += m;
        }
        clock_ = c0 + static_cast<uint32_t>(n);
    }

private:
    struct SplitJob {
        Patch* patch;
        const uint8_t* voices;      // bit 7: the lower tone
        int count;
        float* upper;
        float* lower;
        int n;
    };
#if D5_GATED
    // (the RAM side of render_block's gate, d5_hot.h D5_HOT_GATE; and the other core's job, called from the platform's XIP code)
    static void D5_HOT(part_entry)(Patch* p, float* l, float* r, int n, const Split* split) { p->render_part(l, r, n, split); }
    static void D5_HOT(run_job)(void* arg) {
#else
    static void run_job(void* arg) {
#endif
        const SplitJob& j = *static_cast<const SplitJob*>(arg);
        D5_MARK(5);
        j.patch->render_voices(j.voices, j.count, j.upper, j.lower, j.n);
        D5_MARK(6);
    }
    void D5_HOT(render_voices)(const uint8_t* voices, int count, float* upper, float* lower, int n) {
        for (int k = 0; k < count; ++k) {
            if (voices[k] & 0x80U) lower_.render_voice(voices[k] & 0x7F, lower, n);
            else upper_.render_voice(voices[k], upper, n);
        }
    }
    void D5_HOT(render_part)(float* l, float* r, int n, const Split* split) {
        D5_MARK(0);
        upper_.begin_block(n);
        lower_.begin_block(n);
        D5_MARK(7);
        uint8_t all[24], mine[24], theirs[24];
        int count = upper_.active_voices(all);
        const int lower_count = lower_.active_voices(all + count);
        for (int i = 0; i < lower_count; ++i) all[count + i] = static_cast<uint8_t>(0x80U | all[count + i]);
        count += lower_count;
        // share16/16 of them to the other core, spread through the list (upper and lower alike)
        const int s16 = split ? (split->share16 < 0 ? 0 : (split->share16 > 16 ? 16 : split->share16)) : 8;
        const int want = (count * s16 + 8) / 16;
        int nm = 0, nt = 0;
        for (int i = 0; i < count; ++i) {
            if (want && (i + 1) * want / count > i * want / count) theirs[nt++] = all[i];
            else mine[nm++] = all[i];
        }
        for (int t = 0; t < n; ++t) acc_[0][t] = acc_[1][t] = acc_[2][t] = acc_[3][t] = 0.0f;
        job_ = SplitJob{this, theirs, nt, acc_[2], acc_[3], n};
        D5_MARK(1);
        const bool handed = nt > 0 && split && split->begin && split->wait && split->begin(&run_job, &job_);
        if (!handed) render_voices(theirs, nt, acc_[2], acc_[3], n);
        float uw, lw;
        int om;
        mix_weights(uw, lw, om);
#if D5_FX_LAG
        // fm1-d50: the effects run D5_FX_LAG samples behind the voices, on sums already made, so this core runs them while the other core
        // renders its voices (before, they waited for the other core and ran after it). The output is the same, that much later.
        for (int t = 0; t < n; ++t) {
            const int at = (lag_head_ + t) & (kLagRing - 1);
            float ul, ur, ll, lr;
            upper_.finish_sample(lag_[0][at], ul, ur);
            lower_.finish_sample(lag_[1][at], ll, lr);
            mix_with(uw, lw, om, ul, ur, ll, lr, l[t], r[t]);
        }
        D5_MARK(2);
        render_voices(mine, nm, acc_[0], acc_[1], n);
        if (handed) split->wait();
        D5_MARK(3);
        for (int t = 0; t < n; ++t) {
            const int at = (lag_head_ + D5_FX_LAG + t) & (kLagRing - 1);
            lag_[0][at] = acc_[0][t] + acc_[2][t];
            lag_[1][at] = acc_[1][t] + acc_[3][t];
        }
        lag_head_ = (lag_head_ + n) & (kLagRing - 1);
#else
        render_voices(mine, nm, acc_[0], acc_[1], n);
        D5_MARK(2);
        if (handed) split->wait();
        D5_MARK(3);
        for (int t = 0; t < n; ++t) {
            float ul, ur, ll, lr;
            upper_.finish_sample(acc_[0][t] + acc_[2][t], ul, ur);
            lower_.finish_sample(acc_[1][t] + acc_[3][t], ll, lr);
            mix_with(uw, lw, om, ul, ur, ll, lr, l[t], r[t]);
        }
#endif
        D5_MARK(4);
    }
#if D5_FX_LAG
    static constexpr int kLagRing = 256;    // a power of two (the index is masked), at least D5_FX_LAG + a block
    static_assert(D5_FX_LAG >= Tone<16>::kMaxBlock && D5_FX_LAG + Tone<16>::kMaxBlock <= kLagRing && (kLagRing & (kLagRing - 1)) == 0,
                  "a part's reads and writes may not overlap");
    float lag_[2][kLagRing]{};              // the voices' sums, upper and lower, waiting D5_FX_LAG samples for the effects
    int lag_head_ = 0;
#endif
    SplitJob job_{};
    int cap_ = 16;                          // fm1-d50: cap_voices
    float acc_[4][Tone<16>::kMaxBlock]{};   // upper and lower sums: this core's, the other's

public:
    // The tones' outputs into the patch's two outputs (the rest of next_stereo, shared with render_block).
    D5_INL void mix_weights(float& uw_out, float& lw_out, int& om_out) const {
        // Tone balance per the firmware's mixer (bank code 0xB397): each
        // tone's factor is min(4*b, 255)/200 of its side, so the center
        // is 1.0 each and a full tilt reaches +2.1 dB on the loud side --
        // in whole mode both stay at 1.0 (the ROM forces factor 200).
        // Direction PROVEN: the copy routine at 0x686E feeds pb33's
        // factor into the gain words written to the upper tone's DSP
        // slots -- above 50 the upper tone wins.
        float uw = 1.0f, lw = 1.0f;
        if (spec_.key_mode != KeyMode::kWhole) {
            const float b = spec_.balance < 0 ? 0 : (spec_.balance > 1 ? 1 : spec_.balance);
            uw = 2.0f * b; if (uw > 1.275f) uw = 1.275f;
            lw = 2.0f * (1.0f - b); if (lw > 1.275f) lw = 1.275f;
        }
        uw_out = uw;
        lw_out = lw;
        // Output mode gains, measured on Roland's D-50 VST with one tone at
        // a time (Stereo Polysynth and Arco Strings, dry): mode 1 puts each
        // tone at HALF amplitude on both outputs, modes 2 to 4 put each tone
        // at full amplitude on its own output -- 5.8 dB apart, the EPROM
        // mixer's rows 0x80 against 0xFF. A whole patch ignores the output
        // mode: both outputs, mode-1 level, in every mode. The reverb send
        // is both tones at half in modes 1 and 2 (rows 80 80) and one tone
        // at full in modes 3 and 4.
        om_out = spec_.key_mode == KeyMode::kWhole ? 0 : spec_.output_mode;
    }
    void D5_HOT(mix)(float ul, float ur, float ll, float lr, float& l, float& r) {
        float uw, lw;
        int om;
        mix_weights(uw, lw, om);
        mix_with(uw, lw, om, ul, ur, ll, lr, l, r);
    }
    void D5_HOT(mix_with)(float uw, float lw, int om, float ul, float ur, float ll, float lr, float& l, float& r) {
        if (D5_SKIP(1)) {
            l = saturate((ul * uw + ll * lw) * 0.5f * spec_.volume);
            r = saturate((ur * uw + lr * lw) * 0.5f * spec_.volume);
            return;
        }
        if (om == 0) {
            const float xl = 0.5f * (ul * uw + ll * lw);
            const float xr = 0.5f * (ur * uw + lr * lw);
            reverb_.process(xl, xl, xr, l, r);
        } else {
            // Lower left, Upper right, each tone as its L/MONO signal. Mode
            // 2 sends both tones and its return reaches both outputs (the
            // VST puts the Upper's tail on the left at -48 dB, on the right
            // at -41). Modes 3 and 4 send one tone, and its return stays on
            // that tone's own side -- the other channel is digitally silent
            // in the VST -- while the other tone goes to its output dry.
            const float lo = ll * lw, up = ul * uw;
            if (om == 1) {
                reverb_.process(0.5f * (lo + up), lo, up, l, r);
            } else if (om == 2) {
                reverb_.process(up, 0.0f, up, l, r);
                l = lo;
            } else {
                reverb_.process(lo, lo, 0.0f, l, r);
                r = up;
            }
        }
        l = saturate(l * spec_.volume);
        r = saturate(r * spec_.volume);
    }

    // Sixteen voices plus a reverb tail can ask for more than full scale, and
    // a converter answers that with hard clipping. This stays linear below
    // -3 dB and bends smoothly above, so loud chords lose their peaks instead
    // of tearing.
    D5_INL static float saturate(float x) {
        constexpr float kKnee = 0.7f;
        const float a = x < 0.0f ? -x : x;
        if (a <= kKnee) return x;
        const float over = (a - kKnee) / (1.0f - kKnee);
        const float shaped = kKnee + (1.0f - kKnee) * (over / (1.0f + over));
        return x < 0.0f ? -shaped : shaped;
    }

    bool sounding() const { return upper_.sounding() || lower_.sounding(); }
    // Diagnostic: how many slots are actually rendering right now.
    int voices_sounding() const {
        return upper_.voices_sounding() + lower_.voices_sounding();
    }

    // CPU governor: retire one ringing tail, taking it from whichever tone
    // is carrying more of them.
    bool shed_voice() {
        if (upper_.voices_sounding() >= lower_.voices_sounding()) {
            return upper_.shed_oldest_tail() || lower_.shed_oldest_tail();
        }
        return lower_.shed_oldest_tail() || upper_.shed_oldest_tail();
    }
    // fm1-d50: a held voice, from the tone with more of them sounding (Tone::shed_oldest_held)
    bool shed_held_voice() {
        if (upper_.voices_sounding() >= lower_.voices_sounding()) {
            return upper_.shed_oldest_held() || lower_.shed_oldest_held();
        }
        return lower_.shed_oldest_held() || upper_.shed_oldest_held();
    }

    // Panel controls that apply while the patch is playing. Anything that
    // would resize a delay line or restart a voice belongs in configure().
    void set_volume(float v) { spec_.volume = v; }
#ifdef D5_REVERB_EXTERNAL_POOL
    // fm1-d50: the reverb's delay lines in memory the caller owns (d5_effects.h Reverb::attach); call before configure
    void attach_reverb(ReverbSample* seg0, int n0, ReverbSample* seg1, int n1) { reverb_.attach(seg0, n0, seg1, n1); }
#endif
    // Kept in the spec as well, so a later type change (which rebuilds the
    // reverb) does not quietly restore the patch's original balance.
    void set_reverb_balance(float b) {
        spec_.reverb.balance = b;
        reverb_.set_balance(b);
    }
    // Rebuilding empties the delay lines -- a tail in flight is lost, which
    // is what a type change on the original does too.
    void set_reverb_type(int t) {
        spec_.reverb.type = t < 0 ? 0 : (t > 31 ? 31 : t);
        reverb_.configure(spec_.reverb, sr_);
    }
    int reverb_type() const { return spec_.reverb.type; }
    // Chorus and EQ are per TONE on the real machine; the panel here edits
    // both at once and reads the upper tone back, the same simplification
    // the balance has always used.
    void set_chorus_balance(float b) {
        upper_.set_chorus_balance(b);
        lower_.set_chorus_balance(b);
    }
    void set_chorus_type(int t) {
        upper_.set_chorus_type(t);
        lower_.set_chorus_type(t);
    }
    void set_chorus_rate(float r) {
        upper_.set_chorus_rate(r);
        lower_.set_chorus_rate(r);
    }
    void set_chorus_depth(float d) {
        upper_.set_chorus_depth(d);
        lower_.set_chorus_depth(d);
    }
    void set_eq(const EqSpec& e) { upper_.set_eq(e); lower_.set_eq(e); }
    const EqSpec& eq() const { return upper_.eq(); }
    // Tone Balance, pb[33]: read straight by next_stereo each sample.
    void set_tone_balance(float b) { spec_.balance = b; }
    void set_master_cents(float c) {
        upper_.set_master_cents(c);
        lower_.set_master_cents(c);
    }
    void set_bend_semis(float st) {
        upper_.set_bend_semis(st);
        lower_.set_bend_semis(st);
    }
    // fm1-d50: the D-50's mono mode (MIDI mode 4), for the FM-1's d50.cpp: one note's bend (a bender per channel), and the Solo key modes
    // played polyphonically (the firmware's note-handler rows at 0x69EC: after a received Poly, and in mono, where the caller keeps each
    // channel to one note itself)
    void set_note_bend(int note, float st) {
        upper_.set_note_bend(note, st);
        lower_.set_note_bend(note, st);
    }
    int keys_down() const { return upper_.keys_down() + lower_.keys_down(); }   // (diagnostic)
    int tone_keys_down(int tone) const { return tone ? lower_.keys_down() : upper_.keys_down(); }   // (diagnostic: 0 Upper, 1 Lower)
    void set_solo_off(bool off) {
        if (off && !solo_off_) solo_note_[0] = solo_note_[1] = -1;
        solo_off_ = off;
    }
    void set_mod_wheel(float w) {
        upper_.set_wheel(w);
        lower_.set_wheel(w);
    }
    void set_aftertouch(float a) {
        upper_.set_aftertouch(a);
        lower_.set_aftertouch(a);
    }
    void set_porta(bool sw, int time) {
        upper_.set_porta(sw, time);
        lower_.set_porta(sw, time);
    }
    // fm1-d50: one tone's controls (0 Upper, 1 Lower), for the separate modes, where each tone has its own controller set (the
    // firmware's 0xD300 and 0xD308 sets, FM-1 issue #62)
    void set_tone_bend(int tone, float st) { if (tone) lower_.set_bend_semis(st); else upper_.set_bend_semis(st); }
    void set_tone_wheel(int tone, float w) { if (tone) lower_.set_wheel(w); else upper_.set_wheel(w); }
    void set_tone_aftertouch(int tone, float a) { if (tone) lower_.set_aftertouch(a); else upper_.set_aftertouch(a); }
    void set_tone_porta(int tone, bool sw, int time) { if (tone) lower_.set_porta(sw, time); else upper_.set_porta(sw, time); }

private:
    PatchSpec spec_{};
    // Sixteen slots on the upper tone, eight on the lower: the pool is the
    // D-50's own, and only the upper tone can ever be handed all of it.
    Tone<16> upper_{};
    Tone<8> lower_{};
    Reverb reverb_{};
    float sr_ = 32000.0f;
    int solo_note_[2] = {-1, -1}; // the solo modes' held note per tone (0 upper, 1 lower)
    bool solo_off_ = false;       // fm1-d50: the Solo key modes play polyphonically (set_solo_off)

    // The lower tone runs 128 samples (4.0 ms) behind the upper: Roland's
    // D-50 VST starts it exactly that much later on every note, key and
    // waveform tried (cross-correlation +128 at 32 kHz, corr 1.000; onsets
    // 64 against 192). Two identical tones therefore never sum coherently
    // -- a Tines or Warm Strings dual patch came out 6 dB louder here than
    // there -- and a dual patch carries a 4-ms doubling. Note-ons and
    // note-offs for the lower tone queue here, in order, and fire from the
    // sample loop.
    static constexpr uint32_t kLowerLag = 128;
    static constexpr int kPendingMax = 32;
    struct PendingLower {
        uint32_t due;
        float velocity;
        int16_t note;
        bool on;
        bool solo;
    };
    PendingLower pending_[kPendingMax];
    int pend_head_ = 0;
    int pend_count_ = 0;
    uint32_t clock_ = 0;

    void lower_later(int note, float velocity, bool on, bool solo) {
        if (pend_count_ >= kPendingMax) { lower_now(note, velocity, on, solo); return; }
        PendingLower& e = pending_[(pend_head_ + pend_count_) % kPendingMax];
        e.due = clock_ + kLowerLag + 1;   // +1: the upper already sounds in the next sample
        e.velocity = velocity;
        e.note = static_cast<int16_t>(note);
        e.on = on;
        e.solo = solo;
        ++pend_count_;
    }
    void lower_now(int note, float velocity, bool on, bool solo) {
        if (on) {
            if (solo) solo_cut(1, note);
            lower_.note_on(note, velocity);
            if (solo) solo_note_[1] = note;
        } else {
            lower_.note_off(note);
            if (note == solo_note_[1]) solo_note_[1] = -1;
        }
    }
    void service_lower() {
        while (pend_count_ != 0 && pending_[pend_head_].due == clock_) {
            const PendingLower e = pending_[pend_head_];
            pend_head_ = (pend_head_ + 1) % kPendingMax;
            --pend_count_;
            lower_now(e.note, e.velocity, e.on, e.solo);
        }
    }

    // A solo side ends its previous note when a new one arrives.
    void solo_cut(int side, int note) {
        if (solo_note_[side] >= 0 && solo_note_[side] != note) {
            if (side == 0) upper_.note_off(solo_note_[side], true);
            else lower_.note_off(solo_note_[side], true);
        }
    }
};

}  // namespace d5
