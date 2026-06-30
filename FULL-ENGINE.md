# GuildLite: Open-Source Guild Wars 1 Cross-Platform Client

## Project Vision

GuildLite is an ambitious open-source project to create a fully cross-platform (macOS, Linux, Windows) client for Guild Wars 1, complete with a vetted plugin system inspired by RuneLite. This document provides the comprehensive architecture and development roadmap.

---

## Executive Summary

### What We're Building
A **clean-room implementation** of the Guild Wars 1 game client that:
1. Connects to official ArenaNet servers using the existing network protocol
2. Runs natively on macOS, Linux, and Windows
3. Provides a secure, auditable plugin system for community enhancements
4. Maintains full compliance with ArenaNet's Terms of Service

### What We're NOT Building
- A private server or server emulator
- Tools for cheating, botting, or unfair advantage
- Any modifications to game logic or server-side behavior

---

## Part 1: Technical Foundation (From GWToolbox Analysis)

### 1.1 Reverse-Engineered Knowledge Base

The GWToolbox repository provides extensive documentation of Guild Wars 1 internals:

#### Network Protocol
- **200+ packet definitions** in `GWCA/Packets/StoC.h`
- Server-to-Client packet structure with opcodes for:
  - Trade system (0x0000-0x0006)
  - Agent management (0x001E-0x0034)
  - Chat messages (0x005D-0x0061)
  - Skill system (0x00D9-0x00E6)
  - Item system (0x0135-0x0167)
  - Party/guild management
  - Instance control and loading

#### Game State Structures
- **Agent (Living Entity)**: 452 bytes covering position, rotation, velocity, health, energy, effects, equipment
- **Item Structure**: 84 bytes with modifiers, dye, customization
- **Equipment System**: 9 slots with visual tracking
- **Inventory/Bag System**: Typed bags with item arrays

#### Context Managers
```
GameContext (root)
├── AgentContext     - All entities in the world
├── ItemContext      - Inventory and items
├── MapContext       - Pathfinding, static geometry
├── CharContext      - Player character data
├── PartyContext     - Party members and heroes
├── GuildContext     - Guild information
├── TradeContext     - Trading state
├── WorldContext     - World/instance state
└── AccountContext   - Account-level data
```

#### Rendering
- DirectX 9 integration
- Custom overlay drawing post-render
- Field of view calculations
- Viewport management

---

## Part 2: Architecture Overview

### 2.1 High-Level System Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              GUILDLITE CLIENT                                │
├─────────────────────────────────────────────────────────────────────────────┤
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                         PLUGIN LAYER                                  │   │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐        │   │
│  │  │ Toolbox │ │ Minimap │ │  Builds │ │  Timer  │ │  ...    │        │   │
│  │  └────┬────┘ └────┬────┘ └────┬────┘ └────┬────┘ └────┬────┘        │   │
│  │       └───────────┴───────────┴───────────┴───────────┘              │   │
│  │                              │                                        │   │
│  │  ┌───────────────────────────┴───────────────────────────────────┐   │   │
│  │  │                    PLUGIN API (Sandboxed)                      │   │   │
│  │  │  • Read-only game state access                                 │   │   │
│  │  │  • UI overlay rendering                                        │   │   │
│  │  │  • Event subscription                                          │   │   │
│  │  │  • NO memory modification, NO packet injection                 │   │   │
│  │  └───────────────────────────────────────────────────────────────┘   │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
│                                     │                                        │
├─────────────────────────────────────┴───────────────────────────────────────┤
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                        CORE ENGINE LAYER                              │   │
│  │                                                                       │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐               │   │
│  │  │   RENDERER   │  │   NETWORK    │  │    AUDIO     │               │   │
│  │  │              │  │              │  │              │               │   │
│  │  │ • Vulkan/MTL │  │ • GW Protocol│  │ • OpenAL     │               │   │
│  │  │ • Scene Graph│  │ • Encryption │  │ • Streaming  │               │   │
│  │  │ • UI System  │  │ • Compression│  │ • Effects    │               │   │
│  │  └──────────────┘  └──────────────┘  └──────────────┘               │   │
│  │                                                                       │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐               │   │
│  │  │ GAME STATE   │  │    INPUT     │  │   ASSETS     │               │   │
│  │  │              │  │              │  │              │               │   │
│  │  │ • Entities   │  │ • Keyboard   │  │ • DAT Parser │               │   │
│  │  │ • World      │  │ • Mouse      │  │ • Model Load │               │   │
│  │  │ • Inventory  │  │ • Controller │  │ • Texture    │               │   │
│  │  └──────────────┘  └──────────────┘  └──────────────┘               │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
│                                     │                                        │
├─────────────────────────────────────┴───────────────────────────────────────┤
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                      PLATFORM ABSTRACTION LAYER                       │   │
│  │                                                                       │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐               │   │
│  │  │    WINDOW    │  │   GRAPHICS   │  │   SYSTEM     │               │   │
│  │  │              │  │              │  │              │               │   │
│  │  │ • SDL2/GLFW  │  │ • Vulkan     │  │ • Filesystem │               │   │
│  │  │ • Events     │  │ • Metal      │  │ • Threading  │               │   │
│  │  │ • Clipboard  │  │ • DirectX12  │  │ • Networking │               │   │
│  │  └──────────────┘  └──────────────┘  └──────────────┘               │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
│                                     │                                        │
├─────────────────────────────────────┴───────────────────────────────────────┤
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐                       │
│  │    macOS     │  │    Linux     │  │   Windows    │                       │
│  │   (ARM/x64)  │  │ (x64/ARM)    │  │    (x64)     │                       │
│  └──────────────┘  └──────────────┘  └──────────────┘                       │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 Technology Stack

| Component | Technology | Rationale |
|-----------|------------|-----------|
| **Language** | Rust | Memory safety, cross-platform, performance |
| **Build System** | Cargo + CMake (for C deps) | Native Rust tooling |
| **Graphics** | wgpu (Vulkan/Metal/DX12) | Cross-platform GPU abstraction |
| **Windowing** | winit | Native Rust window management |
| **Audio** | rodio + symphonia | Cross-platform audio |
| **Networking** | tokio + custom protocol | Async I/O, GW protocol impl |
| **UI** | egui | Immediate mode, cross-platform |
| **Plugin System** | Lua + WASM | Sandboxed, safe plugins |
| **Asset Loading** | Custom DAT parser | GW1 asset format support |

### 2.3 Directory Structure

```
guildlite/
├── Cargo.toml                    # Workspace manifest
├── crates/
│   ├── guildlite-core/          # Core engine
│   │   ├── src/
│   │   │   ├── lib.rs
│   │   │   ├── game_state/      # Entity, inventory, world state
│   │   │   ├── network/         # Protocol implementation
│   │   │   └── events/          # Event bus
│   │   └── Cargo.toml
│   │
│   ├── guildlite-renderer/      # Graphics engine
│   │   ├── src/
│   │   │   ├── lib.rs
│   │   │   ├── scene/           # Scene graph
│   │   │   ├── models/          # Model rendering
│   │   │   ├── terrain/         # Terrain system
│   │   │   └── ui/              # In-game UI
│   │   └── Cargo.toml
│   │
│   ├── guildlite-assets/        # Asset loading
│   │   ├── src/
│   │   │   ├── lib.rs
│   │   │   ├── dat/             # DAT file parser
│   │   │   ├── models/          # 3D model formats
│   │   │   └── textures/        # Texture formats
│   │   └── Cargo.toml
│   │
│   ├── guildlite-audio/         # Audio engine
│   │   ├── src/
│   │   │   ├── lib.rs
│   │   │   ├── music/           # Background music
│   │   │   └── effects/         # Sound effects
│   │   └── Cargo.toml
│   │
│   ├── guildlite-network/       # Network layer
│   │   ├── src/
│   │   │   ├── lib.rs
│   │   │   ├── protocol/        # GW protocol structs
│   │   │   ├── packets/         # Packet definitions
│   │   │   ├── encryption/      # RC4 + custom crypto
│   │   │   └── connection/      # Connection management
│   │   └── Cargo.toml
│   │
│   ├── guildlite-plugins/       # Plugin system
│   │   ├── src/
│   │   │   ├── lib.rs
│   │   │   ├── api/             # Plugin API definitions
│   │   │   ├── sandbox/         # WASM sandbox
│   │   │   ├── registry/        # Plugin management
│   │   │   └── verification/    # Signature verification
│   │   └── Cargo.toml
│   │
│   └── guildlite-client/        # Main application
│       ├── src/
│       │   ├── main.rs
│       │   ├── app.rs           # Application state
│       │   └── ui/              # Main UI
│       └── Cargo.toml
│
├── plugins/                      # Official plugins
│   ├── toolbox/                 # GWToolbox reimplementation
│   ├── minimap-enhanced/
│   └── build-templates/
│
├── docs/
│   ├── ARCHITECTURE.md          # This file
│   ├── PROTOCOL.md              # Network protocol docs
│   ├── PLUGIN_API.md            # Plugin development guide
│   └── CONTRIBUTING.md
│
└── tools/
    ├── dat-explorer/            # DAT file inspection tool
    ├── packet-analyzer/         # Network packet analysis
    └── asset-extractor/         # Asset extraction utility
```

---

## Part 3: Development Phases

### Phase 1: Research & Protocol Documentation
**Duration Estimate: 3-6 months**
**Team Size: 2-4 developers**

#### Objectives
1. Complete network protocol documentation
2. Understand authentication and encryption
3. Document DAT file format
4. Create packet analyzer tool

#### Deliverables

##### 1.1 Network Protocol Analysis
```rust
// Example: Documented packet structure
#[derive(Debug, Clone, PacketRead, PacketWrite)]
#[packet(opcode = 0x001E)]
pub struct AgentSpawn {
    pub agent_id: u32,
    pub position: Vec3,
    pub rotation: f32,
    pub model_id: u32,
    pub agent_type: AgentType,
    // ... additional fields from GWCA analysis
}
```

##### 1.2 Authentication Flow
```
┌──────────┐         ┌──────────────┐         ┌──────────────┐
│  Client  │         │  Auth Server │         │  Game Server │
└────┬─────┘         └──────┬───────┘         └──────┬───────┘
     │                      │                        │
     │  1. Login Request    │                        │
     │─────────────────────>│                        │
     │                      │                        │
     │  2. Challenge        │                        │
     │<─────────────────────│                        │
     │                      │                        │
     │  3. Response (SRP?)  │                        │
     │─────────────────────>│                        │
     │                      │                        │
     │  4. Session Token    │                        │
     │<─────────────────────│                        │
     │                      │                        │
     │  5. Connect with Token                        │
     │──────────────────────────────────────────────>│
     │                      │                        │
     │  6. Session Established                       │
     │<──────────────────────────────────────────────│
```

##### 1.3 DAT File Format Research
- File header structure
- Compression algorithms (likely LZMA/Deflate)
- Asset indexing and lookup
- Model format (vertices, UV, bones)
- Texture format (DDS variants)
- Sound format (MP3/OGG)

#### Phase 1 Tasks

| Task | Description | Complexity |
|------|-------------|------------|
| Protocol Capture | Set up network capture between official client and servers | Medium |
| Packet Documentation | Document all packet structures from GWCA + captures | High |
| Auth Research | Understand authentication/encryption mechanism | Very High |
| DAT Parser | Create tool to parse and extract DAT contents | High |
| Model Research | Document 3D model format | High |
| Texture Research | Document texture format and loading | Medium |

---

### Phase 2: Core Engine Foundation
**Duration Estimate: 6-9 months**
**Team Size: 3-5 developers**

#### Objectives
1. Implement platform abstraction layer
2. Create basic rendering pipeline
3. Implement game state management
4. Build asset loading system

#### 2.1 Platform Abstraction

```rust
// Platform trait for OS-specific functionality
pub trait Platform: Send + Sync {
    fn create_window(&self, config: WindowConfig) -> Result<Window>;
    fn get_graphics_backend(&self) -> GraphicsBackend;
    fn get_audio_backend(&self) -> AudioBackend;
    fn get_system_info(&self) -> SystemInfo;
}

// Platform implementations
pub struct MacOSPlatform;
pub struct LinuxPlatform;
pub struct WindowsPlatform;

impl Platform for MacOSPlatform {
    fn get_graphics_backend(&self) -> GraphicsBackend {
        GraphicsBackend::Metal
    }
    // ...
}
```

#### 2.2 Rendering Pipeline

```rust
pub struct Renderer {
    device: wgpu::Device,
    queue: wgpu::Queue,
    surface: wgpu::Surface,

    // Render passes
    terrain_pass: TerrainPass,
    model_pass: ModelPass,
    effect_pass: EffectPass,
    ui_pass: UiPass,
}

impl Renderer {
    pub fn render_frame(&mut self, state: &GameState) -> Result<()> {
        let mut encoder = self.device.create_command_encoder(&Default::default());

        // 1. Render terrain
        self.terrain_pass.render(&mut encoder, &state.map);

        // 2. Render models (agents, props)
        self.model_pass.render(&mut encoder, &state.agents);

        // 3. Render effects (particles, skills)
        self.effect_pass.render(&mut encoder, &state.effects);

        // 4. Render UI overlay
        self.ui_pass.render(&mut encoder, &state.ui);

        self.queue.submit(std::iter::once(encoder.finish()));
        Ok(())
    }
}
```

#### 2.3 Game State Management

```rust
// Mirrors GWCA context structure
pub struct GameState {
    pub agents: AgentContext,
    pub items: ItemContext,
    pub map: MapContext,
    pub character: CharacterContext,
    pub party: PartyContext,
    pub guild: GuildContext,
    pub trade: TradeContext,
    pub world: WorldContext,
}

// Agent structure based on GWCA (452 bytes)
#[derive(Debug, Clone)]
pub struct Agent {
    pub id: AgentId,
    pub position: Vec3,
    pub rotation: f32,
    pub velocity: Vec2,
    pub health: HealthState,
    pub energy: EnergyState,
    pub model_state: ModelState,
    pub effects: EffectFlags,
    pub profession: ProfessionData,
    pub equipment: Equipment,
    pub agent_type: AgentType,
}

// Effect flags (from GWCA)
bitflags! {
    pub struct EffectFlags: u16 {
        const BLEEDING = 0x0001;
        const CONDITION = 0x0002;
        const CRIPPLED = 0x0004;
        const DEAD = 0x0008;
        const DEEP_WOUND = 0x0010;
        const POISON = 0x0020;
        const ENCHANTED = 0x0040;
        const HEXED = 0x0080;
        const WEAPON_SPELL = 0x0100;
    }
}
```

#### 2.4 Asset Loading Pipeline

```rust
pub struct AssetManager {
    dat_archive: DatArchive,
    cache: AssetCache,
    loader: AsyncLoader,
}

impl AssetManager {
    /// Load model by file ID (from GWCA model_file_id)
    pub async fn load_model(&self, file_id: u32) -> Result<Model> {
        if let Some(model) = self.cache.get_model(file_id) {
            return Ok(model.clone());
        }

        let data = self.dat_archive.extract(file_id)?;
        let model = self.parse_model(&data)?;
        self.cache.insert_model(file_id, model.clone());
        Ok(model)
    }

    /// Parse GW1 model format
    fn parse_model(&self, data: &[u8]) -> Result<Model> {
        // Header parsing
        // Vertex data extraction
        // UV coordinate mapping
        // Bone/skeleton data
        // Animation data
        todo!()
    }
}
```

#### Phase 2 Tasks

| Task | Description | Complexity |
|------|-------------|------------|
| Window System | Cross-platform window creation with winit | Low |
| Graphics Init | wgpu initialization for all platforms | Medium |
| Basic Rendering | Simple triangle/quad rendering | Low |
| Terrain System | Terrain mesh rendering | High |
| Model Loader | Parse GW1 3D models from DAT | Very High |
| Texture Loader | Parse and load textures | Medium |
| Camera System | 3D camera with GW1 controls | Medium |
| Input System | Keyboard/mouse handling | Low |

---

### Phase 3: Network Layer Implementation
**Duration Estimate: 6-8 months**
**Team Size: 2-3 developers**

#### Objectives
1. Implement GW1 network protocol
2. Handle authentication
3. Manage connection state
4. Implement packet processing

#### 3.1 Protocol Implementation

```rust
// Packet trait for serialization
pub trait Packet: Sized {
    const OPCODE: u16;
    fn read(reader: &mut PacketReader) -> Result<Self>;
    fn write(&self, writer: &mut PacketWriter) -> Result<()>;
}

// Connection manager
pub struct Connection {
    stream: TcpStream,
    encryption: EncryptionState,
    state: ConnectionState,
}

impl Connection {
    pub async fn send<P: Packet>(&mut self, packet: &P) -> Result<()> {
        let mut buffer = Vec::new();
        let mut writer = PacketWriter::new(&mut buffer);

        writer.write_u16(P::OPCODE)?;
        packet.write(&mut writer)?;

        let encrypted = self.encryption.encrypt(&buffer)?;
        self.stream.write_all(&encrypted).await?;
        Ok(())
    }

    pub async fn recv(&mut self) -> Result<RawPacket> {
        let mut header = [0u8; 4];
        self.stream.read_exact(&mut header).await?;

        let size = u16::from_le_bytes([header[0], header[1]]) as usize;
        let mut data = vec![0u8; size];
        self.stream.read_exact(&mut data).await?;

        let decrypted = self.encryption.decrypt(&data)?;
        Ok(RawPacket::new(decrypted))
    }
}
```

#### 3.2 Packet Definitions (from GWCA)

```rust
// Examples from StoC.h analysis

#[derive(Packet)]
#[packet(opcode = 0x001E)]
pub struct AgentSpawnPacket {
    pub agent_id: u32,
    pub secondary_id: u32,
    pub spawn_type: u8,
    pub unknown1: u8,
    pub allegiance: u8,
    pub weapon_type: u8,
    pub position: Vec3,
    pub plane: u16,
    pub rotation_angle: f32,
    pub rotation_cos: f32,
    pub rotation_sin: f32,
    pub model_state: u8,
    pub speed_modifier: f32,
    pub npc_id: u16,
    pub equipment_data: Option<EquipmentData>,
}

#[derive(Packet)]
#[packet(opcode = 0x005D)]
pub struct ChatMessagePacket {
    pub channel: ChatChannel,
    pub sender_id: u32,
    pub message: GwString,
    pub timestamp: u32,
}

#[derive(Packet)]
#[packet(opcode = 0x00D9)]
pub struct SkillbarUpdatePacket {
    pub agent_id: u32,
    pub slot: u8,
    pub skill_id: u16,
}
```

#### 3.3 Encryption Layer

```rust
// GW1 uses RC4-based encryption with session keys
pub struct EncryptionState {
    rc4_encrypt: Rc4,
    rc4_decrypt: Rc4,
    session_key: [u8; 16],
}

impl EncryptionState {
    pub fn new_from_handshake(server_key: &[u8], client_key: &[u8]) -> Self {
        // Derive session key from Diffie-Hellman or SRP exchange
        // Initialize RC4 state machines
        todo!()
    }

    pub fn encrypt(&mut self, data: &[u8]) -> Vec<u8> {
        self.rc4_encrypt.process(data)
    }

    pub fn decrypt(&mut self, data: &[u8]) -> Vec<u8> {
        self.rc4_decrypt.process(data)
    }
}
```

#### Phase 3 Tasks

| Task | Description | Complexity |
|------|-------------|------------|
| Base Protocol | Packet read/write framework | Medium |
| All Packets | Implement 200+ packet types | Very High |
| Encryption | RC4 + key exchange | High |
| Auth Flow | Complete login sequence | Very High |
| Reconnection | Handle disconnects gracefully | Medium |
| Compression | Packet compression if used | Medium |

---

### Phase 4: Rendering Pipeline (Full)
**Duration Estimate: 9-12 months**
**Team Size: 3-4 developers**

#### Objectives
1. Complete terrain rendering
2. Character/NPC model rendering
3. Animation system
4. Particle/effect system
5. UI rendering

#### 4.1 Terrain Rendering

```rust
pub struct TerrainRenderer {
    chunks: HashMap<ChunkCoord, TerrainChunk>,
    heightmap_texture: wgpu::Texture,
    material_textures: Vec<wgpu::Texture>,
}

pub struct TerrainChunk {
    mesh: wgpu::Buffer,
    index_buffer: wgpu::Buffer,
    material_layers: Vec<MaterialLayer>,
}

impl TerrainRenderer {
    pub fn render(&self, encoder: &mut wgpu::CommandEncoder, camera: &Camera) {
        // Frustum culling
        let visible_chunks = self.chunks.values()
            .filter(|chunk| camera.frustum.contains(&chunk.bounds));

        // Render visible chunks with LOD
        for chunk in visible_chunks {
            let lod = self.calculate_lod(chunk, camera);
            self.render_chunk(encoder, chunk, lod);
        }
    }
}
```

#### 4.2 Character Rendering

```rust
pub struct CharacterRenderer {
    skeleton: Skeleton,
    meshes: Vec<SkinnedMesh>,
    animations: AnimationController,
}

// Skeleton matches GWCA's animation system
pub struct Skeleton {
    bones: Vec<Bone>,
    bind_pose: Vec<Transform>,
}

pub struct AnimationController {
    current_animation: AnimationId,
    blend_tree: AnimationBlendTree,

    // From GWCA: model_state tracking
    model_state: ModelState,
    animation_speed: f32,
}

impl AnimationController {
    pub fn update(&mut self, dt: f32, model_state: ModelState) {
        // Map model_state to animation
        // From GWCA:
        // - 12/76/204 = moving
        // - 96/1088/1120 = attacking
        // - 65/581 = casting
        // - 68/64/100 = idle
        // - 0x8 = dead

        let target_anim = match model_state {
            ModelState::Idle => Animation::Idle,
            ModelState::Moving => Animation::Run,
            ModelState::Attacking => Animation::Attack,
            ModelState::Casting => Animation::Cast,
            ModelState::Dead => Animation::Death,
        };

        self.blend_to(target_anim, 0.2);
    }
}
```

#### 4.3 Effect System

```rust
pub struct EffectSystem {
    particle_pool: ParticlePool,
    skill_effects: HashMap<SkillId, EffectTemplate>,
}

pub struct ParticleEmitter {
    template: ParticleTemplate,
    particles: Vec<Particle>,
    emission_rate: f32,
    lifetime: f32,
}

impl EffectSystem {
    pub fn spawn_skill_effect(&mut self, skill_id: SkillId, position: Vec3, target: Option<Vec3>) {
        if let Some(template) = self.skill_effects.get(&skill_id) {
            self.spawn_effect(template, position, target);
        }
    }
}
```

#### Phase 4 Tasks

| Task | Description | Complexity |
|------|-------------|------------|
| Terrain Mesh | Parse and render terrain | High |
| Terrain LOD | Level of detail system | Medium |
| Character Mesh | Skinned mesh rendering | Very High |
| Animation Loader | Parse animation data | Very High |
| Animation Blend | Smooth animation transitions | High |
| Equipment Render | Attach equipment to characters | High |
| Particle System | GPU particle rendering | High |
| Skill Effects | Visual skill effects | Medium |
| Weather Effects | Rain, snow, fog | Medium |
| Water Rendering | Reflective water surfaces | High |
| Shadow Maps | Dynamic shadows | High |
| Post-Processing | Bloom, HDR, etc. | Medium |

---

### Phase 5: Game Systems
**Duration Estimate: 6-9 months**
**Team Size: 3-5 developers**

#### Objectives
1. Implement all game mechanics client-side
2. Create UI for all game systems
3. Handle all game state updates

#### 5.1 Skill System

```rust
pub struct SkillBar {
    slots: [Option<Skill>; 8],
    attribute_points: HashMap<Attribute, u8>,
}

pub struct Skill {
    pub id: SkillId,
    pub name: String,
    pub profession: Profession,
    pub attribute: Attribute,
    pub activation: f32,
    pub recharge: f32,
    pub energy_cost: u8,
    pub adrenaline_cost: u8,
}

// Recharge tracking from packet 0x00E0
pub struct SkillState {
    pub is_recharging: bool,
    pub recharge_remaining: f32,
}
```

#### 5.2 Inventory System

```rust
// Based on GWCA ItemContext
pub struct Inventory {
    bags: [Bag; 9], // From GWCA bag types
    equipment: Equipment,
    materials: MaterialStorage,
}

pub struct Bag {
    pub bag_type: BagType,
    pub items: Vec<Option<Item>>,
    pub capacity: usize,
}

pub struct Item {
    pub id: ItemId,
    pub model_file_id: u32,
    pub quantity: u16,
    pub modifiers: Vec<ItemModifier>,
    pub dye_info: DyeInfo,
    pub customization: Option<String>,
}
```

#### 5.3 Combat System

```rust
pub struct CombatSystem;

impl CombatSystem {
    pub fn process_packet(&mut self, packet: &GenericValuePacket, state: &mut GameState) {
        // From GWCA P156_Type enum
        match packet.value_type {
            GenericValueType::MeleeAttackFinished => {
                self.on_attack_finished(packet);
            }
            GenericValueType::AttackStarted => {
                self.on_attack_started(packet);
            }
            GenericValueType::SkillDamage | GenericValueType::Damage => {
                self.on_damage(packet);
            }
            GenericValueType::Critical => {
                self.on_critical(packet);
            }
            GenericValueType::KnockedDown => {
                self.on_knockdown(packet);
            }
            // ...
        }
    }
}
```

#### Phase 5 Tasks

| Task | Description | Complexity |
|------|-------------|------------|
| Skill Bar | Skill slot management | Medium |
| Skill Activation | Cast animations, targeting | High |
| Inventory UI | Bag management interface | Medium |
| Item Rendering | 3D item previews | Medium |
| Trading UI | Trade window | Medium |
| Party UI | Party management | Medium |
| Guild UI | Guild window | Medium |
| Map/Compass | Minimap and world map | High |
| Chat System | All chat channels | Medium |
| Quest Log | Quest tracking | Medium |
| Hero Management | Hero skill/equipment | High |

---

### Phase 6: Plugin Architecture
**Duration Estimate: 4-6 months**
**Team Size: 2-3 developers**

This is a critical differentiator - the RuneLite-inspired plugin system.

#### 6.1 Plugin API Design Principles

1. **Read-Only Game State**: Plugins can READ but never MODIFY game state
2. **No Packet Injection**: Plugins cannot send arbitrary packets
3. **Sandboxed Execution**: WASM provides memory isolation
4. **Vetted Contributions**: All plugins go through code review
5. **Signature Verification**: Plugins are cryptographically signed

#### 6.2 Plugin Architecture

```rust
// Plugin API exposed to WASM plugins
pub trait PluginApi {
    // Read-only game state access
    fn get_player_position(&self) -> Position;
    fn get_player_health(&self) -> HealthInfo;
    fn get_nearby_agents(&self) -> Vec<AgentInfo>;
    fn get_inventory(&self) -> InventoryView;
    fn get_skill_bar(&self) -> SkillBarView;

    // UI overlay rendering
    fn draw_overlay(&mut self, ctx: &mut OverlayContext);
    fn register_window(&mut self, window: PluginWindow);

    // Event subscriptions (read-only)
    fn on_agent_spawn(&mut self, callback: fn(AgentSpawnEvent));
    fn on_chat_message(&mut self, callback: fn(ChatMessageEvent));
    fn on_skill_used(&mut self, callback: fn(SkillUsedEvent));
    fn on_map_change(&mut self, callback: fn(MapChangeEvent));

    // Plugin storage (persistent settings)
    fn get_storage(&self) -> &PluginStorage;
    fn set_storage(&mut self, key: &str, value: &str);

    // EXPLICITLY NOT EXPOSED:
    // - Packet sending
    // - Memory modification
    // - Input injection
    // - File system access (except storage)
    // - Network access
}
```

#### 6.3 Plugin Loading System

```rust
pub struct PluginManager {
    runtime: wasmtime::Engine,
    plugins: Vec<LoadedPlugin>,
    registry: PluginRegistry,
    verifier: SignatureVerifier,
}

impl PluginManager {
    pub fn load_plugin(&mut self, path: &Path) -> Result<PluginId> {
        // 1. Verify signature
        let manifest = self.read_manifest(path)?;
        if !self.verifier.verify(&manifest.signature, &manifest.hash)? {
            return Err(PluginError::InvalidSignature);
        }

        // 2. Check against approved plugin list
        if !self.registry.is_approved(&manifest.id, &manifest.version)? {
            return Err(PluginError::NotApproved);
        }

        // 3. Load WASM module in sandbox
        let module = wasmtime::Module::from_file(&self.runtime, path)?;
        let instance = self.instantiate_sandboxed(module)?;

        // 4. Initialize plugin
        let plugin = LoadedPlugin {
            id: manifest.id,
            name: manifest.name,
            version: manifest.version,
            instance,
        };

        plugin.call_init()?;

        let id = self.plugins.len();
        self.plugins.push(plugin);
        Ok(PluginId(id))
    }
}
```

#### 6.4 Plugin Registry and Vetting

```rust
pub struct PluginRegistry {
    approved_plugins: HashMap<PluginId, ApprovedPlugin>,
    signing_keys: Vec<PublicKey>,
}

#[derive(Debug)]
pub struct ApprovedPlugin {
    pub id: String,
    pub name: String,
    pub version: Version,
    pub author: String,
    pub source_url: String,  // Link to auditable source
    pub approval_date: DateTime,
    pub hash: [u8; 32],      // SHA-256 of WASM binary
    pub signature: Signature,
}

// Approval process:
// 1. Developer submits PR to plugins repository
// 2. Automated tests run (no forbidden API calls)
// 3. Manual code review by maintainers
// 4. If approved, plugin is signed and added to registry
// 5. Client downloads registry updates and verifies signatures
```

#### 6.5 Example Plugin: Enhanced Minimap

```rust
// plugins/minimap-enhanced/src/lib.rs
use guildlite_plugin_api::prelude::*;

#[plugin]
pub struct EnhancedMinimap {
    show_quest_markers: bool,
    show_resource_nodes: bool,
    custom_markers: Vec<CustomMarker>,
}

impl Plugin for EnhancedMinimap {
    fn on_init(&mut self, api: &PluginApi) {
        // Register settings window
        api.register_settings_panel("Enhanced Minimap", self.render_settings);
    }

    fn on_frame(&mut self, api: &PluginApi, ctx: &mut OverlayContext) {
        let player_pos = api.get_player_position();
        let nearby = api.get_nearby_agents();

        // Draw enhanced minimap overlay
        for agent in nearby {
            if agent.is_quest_target && self.show_quest_markers {
                ctx.draw_marker(agent.position, MarkerStyle::Quest);
            }
        }

        for marker in &self.custom_markers {
            ctx.draw_marker(marker.position, marker.style);
        }
    }

    fn render_settings(&mut self, ui: &mut Ui) {
        ui.checkbox(&mut self.show_quest_markers, "Show Quest Markers");
        ui.checkbox(&mut self.show_resource_nodes, "Show Resource Nodes");
    }
}
```

#### 6.6 Security Boundaries

```
┌─────────────────────────────────────────────────────────────────┐
│                    PLUGIN SANDBOX (WASM)                        │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │                    PLUGIN CODE                           │    │
│  │                                                          │    │
│  │  • Cannot access host memory directly                   │    │
│  │  • Cannot make system calls                             │    │
│  │  • Cannot access network                                │    │
│  │  • Cannot access filesystem                             │    │
│  │  • Limited CPU time per frame                           │    │
│  │  • Limited memory allocation                            │    │
│  └──────────────────────────┬──────────────────────────────┘    │
│                             │                                    │
│                   ┌─────────┴─────────┐                         │
│                   │    PLUGIN API     │                         │
│                   │   (Gatekeeper)    │                         │
│                   │                   │                         │
│                   │ • Read-only views │                         │
│                   │ • UI overlay only │                         │
│                   │ • Event callbacks │                         │
│                   └─────────┬─────────┘                         │
│                             │                                    │
└─────────────────────────────┼────────────────────────────────────┘
                              │ Controlled Interface
┌─────────────────────────────┴────────────────────────────────────┐
│                      CORE ENGINE                                 │
│                                                                  │
│  Full access to:                                                 │
│  • Game state                                                    │
│  • Network layer                                                 │
│  • Rendering                                                     │
│  • Platform APIs                                                 │
└─────────────────────────────────────────────────────────────────┘
```

#### Phase 6 Tasks

| Task | Description | Complexity |
|------|-------------|------------|
| WASM Runtime | Integrate wasmtime | Medium |
| Plugin API | Define stable API | High |
| Sandbox Security | Memory/CPU limits | High |
| Plugin Loader | Load and verify plugins | Medium |
| Signature System | Ed25519 signing | Medium |
| Registry Server | Plugin distribution | Medium |
| Review Process | GitHub Actions for testing | Medium |
| Toolbox Port | Port GWToolbox features | Very High |

---

### Phase 7: Platform-Specific Polish
**Duration Estimate: 3-4 months**
**Team Size: 2-3 developers**

#### 7.1 macOS

```rust
#[cfg(target_os = "macos")]
mod macos {
    pub fn setup_app_bundle() {
        // Info.plist configuration
        // Code signing
        // Notarization
        // Retina display support
        // Touch Bar support (if applicable)
        // Menu bar integration
    }
}
```

- **Metal rendering backend** (via wgpu)
- **Apple Silicon (M1/M2/M3)** native support
- **.app bundle** with proper signing and notarization
- **Retina display** support
- **macOS-native** file dialogs and notifications

#### 7.2 Linux

```rust
#[cfg(target_os = "linux")]
mod linux {
    pub fn setup_linux_integration() {
        // Flatpak/AppImage packaging
        // XDG directory standards
        // Wayland + X11 support
        // PulseAudio/PipeWire audio
    }
}
```

- **Vulkan rendering** (primary), OpenGL fallback
- **Wayland** and **X11** window support
- **Flatpak** and **AppImage** distribution
- **Steam Deck** compatibility
- **PipeWire/PulseAudio** audio

#### 7.3 Windows

```rust
#[cfg(target_os = "windows")]
mod windows {
    pub fn setup_windows_integration() {
        // DirectX 12 backend
        // Windows installer (MSI/MSIX)
        // Windows Hello integration (optional)
        // Jump list support
    }
}
```

- **DirectX 12** rendering (via wgpu)
- **MSIX/MSI** installer
- **Windows 10/11** optimizations

#### Phase 7 Tasks

| Task | Description | Complexity |
|------|-------------|------------|
| macOS Bundle | Code signing, notarization | Medium |
| Linux Packages | Flatpak, AppImage, .deb | Medium |
| Windows Installer | MSIX package | Medium |
| Platform Testing | QA on all platforms | High |
| Performance Tuning | Platform-specific optimizations | High |

---

## Part 4: Legal and Compliance Framework

### 4.1 Clean-Room Implementation

This project uses a **clean-room approach**:

1. **No copyrighted code**: We don't copy any ArenaNet code
2. **Protocol documentation**: The network protocol is documented through observation
3. **Asset loading**: We read the user's legally-owned game files
4. **No game logic reimplementation**: We're a client, not a server

### 4.2 Terms of Service Compliance

| Concern | Our Approach |
|---------|--------------|
| **Third-party programs** | Plugins are read-only overlays, not automation |
| **Botting** | No automation APIs exposed to plugins |
| **Exploitation** | No memory modification or packet injection |
| **Reverse engineering** | Protocol observation is legal (Sega v. Accolade) |

### 4.3 Plugin Approval Criteria

Plugins MUST NOT:
1. Automate gameplay (movement, combat, farming)
2. Provide unfair advantages (ESP through walls, speed hacks)
3. Modify game state or inject packets
4. Access other players' private information
5. Interfere with game economy

Plugins MAY:
1. Display overlay information (DPS meters, timers)
2. Enhance UI (better inventory, skill bars)
3. Provide convenience (build templates, quick travel)
4. Add cosmetic features (custom minimaps, themes)

### 4.4 Suggested ArenaNet Communication

We recommend:
1. Open dialogue with ArenaNet about the project
2. Request blessing/toleration like Jagex did with RuneLite
3. Share plugin vetting process for transparency
4. Offer to implement any restrictions they require

---

## Part 5: Development Timeline Overview

```
Year 1
├── Q1-Q2: Phase 1 - Research & Protocol (3-6 months)
│   └── Deliverable: Complete protocol documentation, DAT parser
│
├── Q3-Q4: Phase 2 - Core Engine (6-9 months, overlapping)
│   └── Deliverable: Basic rendering, asset loading, game state
│
Year 2
├── Q1-Q2: Phase 3 - Network Layer (6-8 months, overlapping)
│   └── Deliverable: Full protocol implementation, authentication
│
├── Q2-Q4: Phase 4 - Rendering Pipeline (9-12 months)
│   └── Deliverable: Complete 3D rendering, animations, effects
│
Year 3
├── Q1-Q3: Phase 5 - Game Systems (6-9 months)
│   └── Deliverable: All game mechanics, complete UI
│
├── Q3-Q4: Phase 6 - Plugin Architecture (4-6 months)
│   └── Deliverable: Plugin system, GWToolbox port
│
├── Q4: Phase 7 - Platform Polish (3-4 months)
│   └── Deliverable: Release-ready builds for all platforms
│
Year 4
├── Q1: MVP Release
└── Ongoing: Maintenance, plugin ecosystem
```

---

## Part 6: Team Structure Recommendation

### Core Team (5-8 people)

| Role | Responsibilities |
|------|-----------------|
| **Project Lead** | Architecture, coordination, ArenaNet communication |
| **Rendering Engineer** | Graphics pipeline, shaders, effects |
| **Network Engineer** | Protocol, encryption, connection management |
| **Engine Developer (x2)** | Core systems, game state, assets |
| **Plugin Architect** | Plugin system, security, API design |
| **Platform Engineer** | Cross-platform, packaging, CI/CD |
| **QA Lead** | Testing, bug triage, release management |

### Community Contributors
- Plugin developers
- Asset format researchers
- Documentation writers
- Translators
- Testers

---

## Part 7: Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| ArenaNet legal action | Medium | Critical | Proactive communication, ToS compliance |
| Protocol changes | Low | High | Abstraction layer, quick update capability |
| Encryption too complex | Medium | High | Community collaboration, research grants |
| DAT format undocumented | Medium | Medium | Existing tools (GW Dat Browser) as reference |
| Performance issues | Medium | Medium | Profiling, optimization passes |
| Plugin security breach | Low | High | WASM sandbox, thorough vetting |

---

## Part 8: Getting Started

### Prerequisites
- Rust 1.70+ with cargo
- Platform-specific tools:
  - **macOS**: Xcode Command Line Tools
  - **Linux**: build-essential, vulkan-tools
  - **Windows**: Visual Studio Build Tools

### Initial Development Steps

```bash
# Clone repository
git clone https://github.com/guildlite/guildlite.git
cd guildlite

# Build all crates
cargo build

# Run packet analyzer tool
cargo run -p packet-analyzer

# Run DAT explorer
cargo run -p dat-explorer

# Run client (when ready)
cargo run -p guildlite-client
```

---

## Conclusion

GuildLite is an ambitious but achievable project. The extensive reverse-engineering work in GWToolbox provides a solid foundation for understanding the Guild Wars 1 client. With careful architecture, legal compliance, and community collaboration, we can create a cross-platform client that:

1. Brings GW1 to macOS and Linux natively
2. Provides a secure, extensible plugin ecosystem
3. Respects ArenaNet's intellectual property and terms of service
4. Enhances the game experience for the dedicated GW1 community

The road is long, but the destination is worth it.

---

*This document is version 1.0 and will be updated as the project evolves.*
