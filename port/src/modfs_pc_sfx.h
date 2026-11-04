/* Only independently verified cross-version event aliases belong here.
 * Equal action/cue numbers in the two executables do not prove identity.
 * DOG001: PC0043D630 locomotion ->00471260(11,1); PS1 cerberus_StepSound
 * -> mono1/spatial12/13 -> NOISE23 program0 waveform2. No sound data. */
static const struct { const char *pc; uint32_t block, vag; } modfs_pc_sfx[] = {
    {"WAV/ENEMY/DOG/SND-001.WAV", 23u, 2u},
};
