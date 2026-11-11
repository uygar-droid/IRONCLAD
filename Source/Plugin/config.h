#pragma once

// iPlug2 configuration for IRONCLAD (VST3 only).
//
// IMPORTANT: PLUG_UNIQUE_ID, PLUG_MFR_ID, PLUG_NAME and PLUG_MFR determine the VST3
// class ID. Changing any of them after release makes DAWs treat IRONCLAD as a
// different plugin and breaks saved projects. They must be final before v1.0.

#define PLUG_NAME "IRONCLAD"
#define PLUG_MFR "Ironclad Audio"
#define PLUG_VERSION_HEX 0x00000200
#define PLUG_VERSION_STR "0.2.0"
#define PLUG_UNIQUE_ID 'Ircl'
#define PLUG_MFR_ID 'IrnA'
#define PLUG_URL_STR "https://github.com/uygar-droid/IRONCLAD"
#define PLUG_EMAIL_STR ""
#define PLUG_COPYRIGHT_STR "Copyright 2026 Ironclad Audio"
#define PLUG_CLASS_NAME IRONCLAD

#define BUNDLE_NAME "IRONCLAD"
#define BUNDLE_MFR "IroncladAudio"
#define BUNDLE_DOMAIN "com"

#define SHARED_RESOURCES_SUBPATH "IRONCLAD"

// mono->mono, mono->stereo, stereo->stereo
#define PLUG_CHANNEL_IO "1-1 1-2 2-2"

#define PLUG_LATENCY 0
#define PLUG_TYPE 0
#define PLUG_DOES_MIDI_IN 0
#define PLUG_DOES_MIDI_OUT 0
#define PLUG_DOES_MPE 0
#define PLUG_DOES_STATE_CHUNKS 1
#define PLUG_HAS_UI 1
#define PLUG_WIDTH 1100
#define PLUG_HEIGHT 640
#define PLUG_FPS 60
#define PLUG_SHARED_RESOURCES 0
#define PLUG_HOST_RESIZE 0

#define VST3_SUBCATEGORY "Fx|Distortion"

#define ROBOTO_FN "Roboto-Regular.ttf"
