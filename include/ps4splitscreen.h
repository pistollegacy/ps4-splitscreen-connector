/**
 * @file ps4splitscreen.h
 * @brief Aggregated public header for PS4SplitScreenConnector
 * 
 * Include this single header to access all public APIs.
 */

#ifndef PS4SPLITSCREEN_H
#define PS4SPLITSCREEN_H

// Main API
#include "ps4splitscreen/ps4splitscreen.h"

// Network
#include "ps4splitscreen/ps4_net.h"

// Synchronization
#include "ps4splitscreen/lockstep_sync.h"
#include "ps4splitscreen/rollback_netcode.h"
#include "ps4splitscreen/state_buffer.h"
#include "ps4splitscreen/deterministic_rng.h"

// Rendering
#include "ps4splitscreen/splitscreen_viewport.h"
#include "ps4splitscreen/ps4_framebuffer.h"
#include "ps4splitscreen/ps4_gnm_helpers.h"

// Input
#include "ps4splitscreen/input_sync.h"

// Time
#include "ps4splitscreen/time_sync.h"

// Logging
#include "ps4splitscreen/logger.h"

#endif // PS4SPLITSCREEN_H