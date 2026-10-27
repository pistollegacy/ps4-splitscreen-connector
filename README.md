# PS4SplitScreenConnector

**LAN Splitscreen Framework for PS4 Homebrew (OpenOrbis/PS4SDK)**

Enables two jailbroken PS4 consoles to synchronize game state over LAN for true splitscreen co-op gameplay. Each PS4 renders its own viewport independently while maintaining deterministic simulation across both consoles.

## Features

- **Two Synchronization Modes**:
  - **Deterministic Lockstep**: Traditional frame-waiting, zero latency on LAN
  - **GGPO-Style Rollback**: Predictive local input, automatic rewind on remote input arrival
- **Reliable UDP Transport**: Sequencing, ACKs, SACK, retransmission, congestion control
- **LAN Discovery**: Broadcast-based session finding
- **Splitscreen Rendering**: Horizontal/vertical layouts with debug overlay
- **Deterministic RNG**: PCG32 for synchronized random numbers
- **Frame-Accurate Time Sync**: NTP-style clock synchronization
- **Input Handling**: Dual controller support with vibration
- **Statistics & Diagnostics**: RTT, jitter, packet loss, rollback counting

## Architecture

```
┌─────────────────┐     LAN (UDP)      ┌─────────────────┐
│   PS4 Host      │ ◄────────────────► │   PS4 Client    │
│                 │   Reliable UDP     │                 │
│ ┌─────────────┐ │                    │ ┌─────────────┐ │
│ │ Game Logic  │ │  Input Packets     │ │ Game Logic  │ │
│ │ (Simulate)  │ │  State Deltas      │ │ (Simulate)  │ │
│ └──────┬──────┘ │  Checkpoints       │ └──────┬──────┘ │
│        │        │                    │        │        │
│ ┌──────▼──────┐ │                    │ ┌──────▼──────┐ │
│ │  Rollback   │ │                    │ │  Rollback   │ │
│ │  Netcode    │ │                    │ │  Netcode    │ │
│ └─────────────┘ │                    │ └─────────────┘ │
│        │        │                    │        │        │
│ ┌──────▼──────┐ │                    │ ┌──────▼──────┐ │
│ │  Renderer   │ │                    │ │  Renderer   │ │
│ │ (Viewport 0)│ │                    │ │ (Viewport 1)│ │
│ └─────────────┘ │                    │ └─────────────┘ │
└─────────────────┘                    └─────────────────┘
```

## Requirements

- **PS4 Jailbreak** (9.00 or lower recommended)
- **OpenOrbis SDK** / **PS4SDK** (open source toolchain)
- **CMake 3.16+**
- **Clang/LLVM** targeting `x86_64-scei-ps4-elf`

## Building

### 1. Setup OpenOrbis Environment

```bash
# Clone and build OpenOrbis toolchain
git clone https://github.com/OpenOrbis/openorbis-sdk
cd openorbis-sdk
./build-toolchain.sh

# Source the environment
source environment.sh
```

### 2. Build the Library

```bash
cd ps4-splitscreen-connector
./scripts/build_ps4.sh Release
```

This creates:
- `install_ps4/lib/libps4splitscreen.a` - Static library
- `install_ps4/include/ps4splitscreen/` - Public headers
- `build_ps4/bin/splitscreen_demo` - Example executable
- `build_ps4/bin/net_stress_test` - Network stress test

### 3. Create PS4 Package (.pkg)

```bash
# Customize package.conf
cp scripts/package.conf.template package.conf
# Edit package.conf with your Content-ID, paths, etc.

# Generate .pkg
orbis-pub-gen -config package.conf -output PS4SplitScreenConnector.pkg
```

### 4. Install on PS4

- Transfer `.pkg` to PS4 via FTP or USB
- Install using Package Installer (Debug Settings → Game → Package Installer)
- Run from homebrew launcher

## Usage

### Basic Integration

```c
#include <ps4splitscreen/ps4splitscreen.h>

// 1. Initialize
ps4splitscreen_net_config_t net_cfg = ps4splitscreen_net_config_default();
ps4splitscreen_sync_config_t sync_cfg = ps4splitscreen_sync_config_default();
sync_cfg.mode = PS4SPLITSCREEN_SYNC_ROLLBACK;  // or LOCKSTEP
sync_cfg.rollback_max_frames = 8;

ps4splitscreen_viewport_config_t vp_cfg = 
    ps4splitscreen_viewport_config_default(PS4SPLITSCREEN_LAYOUT_HORIZONTAL);

ps4splitscreen_context_t* ctx = ps4splitscreen_init(
    &net_cfg, &sync_cfg, &vp_cfg, my_log_callback, my_user_data);

// 2. Register your game state
typedef struct { float x, y; int health; } my_game_state_t;

void serialize(const void* state, uint8_t* buf, size_t* size) {
    const my_game_state_t* gs = state;
    memcpy(buf, gs, sizeof(my_game_state_t));
    *size = sizeof(my_game_state_t);
}

void deserialize(void* state, const uint8_t* buf, size_t size) {
    memcpy(state, buf, sizeof(my_game_state_t));
}

ps4splitscreen_register_game_state(ctx, sizeof(my_game_state_t),
    serialize, deserialize, free, NULL);

// 3. Set callbacks
// (Would use setters exposed by session_manager)

// 4. Start session
// HOST:
ps4splitscreen_host_session(ctx, "My Game", 2);

// CLIENT:
ps4splitscreen_join_session(ctx, "192.168.1.100");

// 5. Main loop
while (running) {
    // Read controllers
    ps4splitscreen_input_update(input_sync);
    
    ps4splitscreen_input_t inputs[1];
    inputs[0] = *ps4splitscreen_input_get(input_sync, local_player_id);
    inputs[0].frame_number = ps4splitscreen_get_frame(ctx);
    
    // Tick framework (handles net + simulation)
    ps4splitscreen_tick(ctx, 16.666f, inputs, 1);
    
    // Render
    for (int p = 0; p < 2; p++) {
        ps4splitscreen_viewport_begin_player(viewport, p, cmd_buffer);
        render_game_for_player(p);
        ps4splitscreen_viewport_end_player(viewport, cmd_buffer);
    }
    
    ps4splitscreen_framebuffer_present(framebuffer, cmd_buffer);
    ps4splitscreen_framebuffer_advance(framebuffer);
}

// 6. Cleanup
ps4splitscreen_shutdown(ctx);
```

### Synchronization Modes

#### Lockstep (PS4SPLITSCREEN_SYNC_LOCKSTEP)
- Waits for both players' inputs before advancing frame
- Zero latency on LAN (< 1ms)
- Simple, deterministic
- Best for: Fighting games, RTS, turn-based

#### Rollback (PS4SPLITSCREEN_SYNC_ROLLBACK) - Recommended
- Predicts local input immediately
- Rewinds & re-simulates when remote input arrives
- Smoother feel, hides latency up to ~100ms
- Best for: Action games, platformers, racing

## API Reference

### Core Types
- `ps4splitscreen_context_t` - Main opaque context
- `ps4splitscreen_input_t` - Controller state (buttons, sticks, triggers)
- `ps4splitscreen_game_state_t` - Your game state (opaque)
- `ps4splitscreen_stats_t` - Network/sync statistics

### Key Functions
| Function | Description |
|----------|-------------|
| `ps4splitscreen_init()` | Initialize framework |
| `ps4splitscreen_shutdown()` | Cleanup |
| `ps4splitscreen_host_session()` | Host a session |
| `ps4splitscreen_join_session()` | Join by IP |
| `ps4splitscreen_discover_sessions()` | LAN discovery |
| `ps4splitscreen_register_game_state()` | Register serialization |
| `ps4splitscreen_tick()` | Main loop (net + sim) |
| `ps4splitscreen_get_game_state()` | Get current synced state |
| `ps4splitscreen_viewport_begin_player()` | Start player render |
| `ps4splitscreen_viewport_end_player()` | End player render |
| `ps4splitscreen_get_stats()` | Get statistics |

## Example Games Included

1. **splitscreen_demo** - Pong-style game demonstrating rollback netcode
2. **net_stress_test** - Bandwidth/latency stress testing

## Network Protocol

- **Port 9337** (UDP): Game traffic (reliable UDP)
- **Port 9338** (UDP): Discovery broadcasts
- **MTU-safe**: 1472 byte max packets
- **Channels**: 4 reliability channels (0=inputs, 1=state, 2=events, 3=video)

## Performance Targets

| Metric | Target |
|--------|--------|
| Latency (LAN) | < 2ms RTT |
| Bandwidth | < 50 KB/s per client |
| Rollback frames | ≤ 8 |
| CPU overhead | < 2% @ 60 FPS |
| Memory | < 2 MB |

## License

MIT License - See LICENSE file

## Credits

- OpenOrbis / PS4SDK community
- GGPO rollback netcode architecture
- PCG random number generator (Melissa O'Neill)

## Support

- Issues: GitHub Issues
- Discord: PS4 Homebrew Discord (#openorbis)
- Wiki: OpenOrbis Wiki

---

**⚠️ Disclaimer**: This software is for educational and homebrew purposes only. 
Use only on your own legally owned PS4 console with appropriate jailbreak.
Respect Sony's Terms of Service and applicable laws.