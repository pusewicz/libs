import CSDL3

/// A vertex as the GPU reads it.
struct BatchVertex {
  var x, y, u, v: Float
  var color: Color
  var overlay: Color
  var params: VertexParams

  @inline(always)
  init(_ position: Vec2, _ uv: Vec2, color: Color, overlay: Color, params: VertexParams) {
    self.x = position.x
    self.y = position.y
    self.u = uv.x
    self.v = uv.y
    self.color = color
    self.overlay = overlay
    self.params = params
  }

  /// The offset of a field in bytes.
  static func offset(of field: PartialKeyPath<BatchVertex>) -> UInt32 {
    UInt32(MemoryLayout<BatchVertex>.offset(of: field)!)
  }
}

/// How the shader treats a vertex. The GPU reads the four bytes as 0 to 1.
struct VertexParams {
  /// 255 ignores the texture.
  var untextured: UInt8
  /// 255 samples sharp.
  var sharp: UInt8
  /// Fills the four bytes.
  var unused: UInt16 = 0

  /// The params of plain color.
  static let plain = VertexParams(untextured: 255, sharp: 0)

  /// The params of a texture with its settings.
  @inline(always)
  init(_ sampling: Sampling) {
    self.init(untextured: 0, sharp: sampling.filter == .sharp ? 255 : 0)
  }

  @inline(always)
  init(untextured: UInt8, sharp: UInt8) {
    self.untextured = untextured
    self.sharp = sharp
  }
}

/// The corners of a quad: the top-left, top-right, bottom-right and
/// bottom-left of its texture area.
struct Quad {
  var topLeft, topRight, bottomRight, bottomLeft: Vec2

  /// Moves the quad so that its top-left corner is on a whole pixel, if `snap`
  /// is true.
  @inline(always)
  func snapped(if snap: Bool) -> Quad {
    snap ? self + ((topLeft + 0.5).rounded(.down) - topLeft) : self
  }

  /// Moves a quad.
  @inline(always)
  static func + (quad: Quad, offset: Vec2) -> Quad {
    Quad(
      topLeft: quad.topLeft + offset, topRight: quad.topRight + offset,
      bottomRight: quad.bottomRight + offset, bottomLeft: quad.bottomLeft + offset)
  }
}

/// Memory for plain values that grows. Draws write their vertices and indices
/// straight into it, as the C version does: one capacity check for each draw,
/// and none for each value.
struct GrowableBuffer<Element: BitwiseCopyable>: ~Copyable {
  private var start: UnsafeMutablePointer<Element>
  private var capacity: Int
  /// The number of values.
  private(set) var count = 0

  init(capacity: Int) {
    self.start = .allocate(capacity: capacity)
    self.capacity = capacity
  }

  deinit {
    start.deallocate()
  }

  /// Adds `n` values at the end, and returns their memory. Initialize all of
  /// them before the next call.
  @inline(always)
  mutating func append(_ n: Int) -> UnsafeMutablePointer<Element> {
    if n > capacity - count {
      grow(minimumCapacity: count + n)
    }
    let values = start + count
    count += n
    return values
  }

  @inline(never)
  private mutating func grow(minimumCapacity: Int) {
    var grown = capacity * 2
    while grown < minimumCapacity {
      grown *= 2
    }
    let moved = UnsafeMutablePointer<Element>.allocate(capacity: grown)
    moved.initialize(from: start, count: count)
    start.deallocate()
    start = moved
    capacity = grown
  }

  /// Drops the values, and keeps the memory.
  mutating func removeAll() {
    count = 0
  }

  /// Calls `body` with the bytes of the values.
  func withUnsafeBytes<Result>(_ body: (UnsafeRawBufferPointer) -> Result) -> Result {
    body(UnsafeRawBufferPointer(start: start, count: count * MemoryLayout<Element>.stride))
  }
}

/// A texture and the sampler of a draw.
struct Binding {
  let texture: Texture
  let sampler: OpaquePointer

  @inline(always)
  func matches(_ texture: Texture, _ sampler: OpaquePointer) -> Bool {
    self.texture === texture && self.sampler == sampler
  }

  var sdl: SDL_GPUTextureSamplerBinding {
    SDL_GPUTextureSamplerBinding(texture: texture.handle, sampler: sampler)
  }
}

/// The state that decides if draws can share a command. It holds no
/// references, so a draw can make and compare it without retains.
struct DrawKey: Equatable {
  var target: ObjectIdentifier
  var shader: ObjectIdentifier
  var blend: Blend
  var clip: PixelRect?
  var uniforms: Range<Int>

  @inline(always)
  static func == (a: DrawKey, b: DrawKey) -> Bool {
    a.target == b.target && a.shader == b.shader && a.blend == b.blend
      && a.uniforms == b.uniforms && a.clip == b.clip
  }
}

/// A recorded clear or draw.
struct Command {
  enum Kind {
    case clear(Color)
    case draw(DrawKey)
  }

  var kind: Kind
  /// The target. The command keeps it until the frame ends.
  var target: Texture
  /// The shader of a draw. The command keeps it until the frame ends.
  var shader: Shader?
  /// Slot 0 is the draw texture. Slots 1 and after are shader textures.
  var bindings: [4 of Binding?] = .init(repeating: nil)
  var bindingCount = 0
  var firstIndex: UInt32 = 0
  var indexCount: UInt32 = 0

  /// The key of a draw, or nil for a clear.
  var drawKey: DrawKey? {
    if case .draw(let key) = kind { key } else { nil }
  }

  /// Adds the indices of a draw if the draw can join this command. It changes
  /// the command in place, so the command is not copied.
  ///
  /// - Parameters:
  ///   - key: The key of the draw.
  ///   - texture: The texture of the draw, or nil for plain color. Plain color
  ///     can use any texture.
  ///   - sampler: The sampler of `texture`.
  ///   - state: The draw state, for the shader textures.
  ///   - samplers: The samplers of the context.
  ///   - white: The texture of empty shader slots.
  ///   - indexCount: The number of indices of the draw.
  /// - Returns: false if the draw needs a new command.
  mutating func join(
    _ key: DrawKey, texture: Texture?, sampler: OpaquePointer?, state: borrowing DrawState,
    samplers: Samplers, white: Texture, indexCount: UInt32
  ) -> Bool {
    guard case .draw(let joined) = kind, joined == key else { return false }
    if let texture, let sampler, !bindings[0]!.matches(texture, sampler) {
      return false
    }
    for slot in 1..<bindingCount {
      let bound = state.shaderTextures[slot] ?? white
      if !bindings[slot]!.matches(bound, samplers[bound.sampling]) {
        return false
      }
    }
    self.indexCount += indexCount
    return true
  }

  /// Returns true if both commands bind the same textures and samplers.
  func hasSameBindings(as other: Command) -> Bool {
    guard bindingCount == other.bindingCount else { return false }
    for slot in 0..<bindingCount {
      guard let binding = bindings[slot], let otherBinding = other.bindings[slot],
        binding.matches(otherBinding.texture, otherBinding.sampler)
      else {
        return false
      }
    }
    return true
  }
}

/// The draws of a frame.
struct Batch: ~Copyable {
  var vertices = GrowableBuffer<BatchVertex>(capacity: Context.minCapacity)
  var indices = GrowableBuffer<UInt32>(capacity: Context.minCapacity)
  /// The commands of the frame before the last one. Call `closeCommands()`
  /// before you read them.
  var commands: [Command] = []
  /// The last command. Draws join it. It is not in `commands`, so a draw
  /// changes it in place without the checks of an array access.
  var lastCommand: Command?
  /// The uniform data of the frame. Each draw uses a range of it.
  var uniforms: [UInt8] = []

  /// Returns true if `vertexCount` more vertices and `indexCount` more indices
  /// fit in 32-bit indices.
  @inline(always)
  func hasRoom(vertexCount: Int, indexCount: Int) -> Bool {
    vertices.count + vertexCount <= UInt32.max && indices.count + indexCount <= UInt32.max
  }

  /// Appends a quad.
  ///
  /// - Parameter uv: The top-left and bottom-right of the texture area.
  mutating func addQuad(
    _ quad: Quad, uv: (Vec2, Vec2), color: Color, overlay: Color, params: VertexParams
  ) {
    let base = UInt32(vertices.count)
    let (uv0, uv1) = uv
    let vertex = vertices.append(4)
    vertex.initialize(
      to: BatchVertex(quad.topLeft, uv0, color: color, overlay: overlay, params: params))
    (vertex + 1).initialize(
      to: BatchVertex(
        quad.topRight, Vec2(uv1.x, uv0.y), color: color, overlay: overlay, params: params))
    (vertex + 2).initialize(
      to: BatchVertex(quad.bottomRight, uv1, color: color, overlay: overlay, params: params))
    (vertex + 3).initialize(
      to: BatchVertex(
        quad.bottomLeft, Vec2(uv0.x, uv1.y), color: color, overlay: overlay, params: params))
    // hasRoom(vertexCount:indexCount:) keeps base + 3 in 32 bits.
    let index = indices.append(6)
    index.initialize(to: base)
    (index + 1).initialize(to: base &+ 1)
    (index + 2).initialize(to: base &+ 2)
    (index + 3).initialize(to: base)
    (index + 4).initialize(to: base &+ 2)
    (index + 5).initialize(to: base &+ 3)
  }

  /// Starts a command after the last one.
  mutating func start(_ command: Command) {
    closeCommands()
    lastCommand = command
  }

  /// Moves the last command into `commands`.
  mutating func closeCommands() {
    if let closed = lastCommand.take() {
      commands.append(closed)
    }
  }

  /// Appends a vertex, and an index for it.
  mutating func addVertex(_ vertex: BatchVertex) {
    indices.append(1).initialize(to: UInt32(vertices.count))
    vertices.append(1).initialize(to: vertex)
  }

  /// Drops the draws, and keeps the memory.
  mutating func removeAll() {
    vertices.removeAll()
    indices.removeAll()
    commands.removeAll(keepingCapacity: true)
    lastCommand = nil
    uniforms.removeAll(keepingCapacity: true)
  }
}

/// The draw state that `Context.push()` saves.
struct DrawState {
  var transform = Transform.identity
  var target: Texture?
  var shader: Shader?
  /// Slot 0 is the draw texture, so it stays nil.
  var shaderTextures: [4 of Texture?] = .init(repeating: nil)
  var uniforms = 0..<0
  var font: Font?
  var clip: PixelRect?
  var snap = true
  var blend = Blend.alpha
}

/// The samplers of a context, by filter and wrap mode.
final class Samplers {
  private let device: Device
  private let handles: [OpaquePointer]

  init(device: Device) throws(PxlError) {
    var handles: [OpaquePointer] = []
    for filter in [SDL_GPU_FILTER_NEAREST, SDL_GPU_FILTER_LINEAR] {
      for wrap in Wrap.allCases {
        var info = SDL_GPUSamplerCreateInfo()
        info.min_filter = filter
        info.mag_filter = filter
        info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST
        info.address_mode_u = wrap.addressMode
        info.address_mode_v = wrap.addressMode
        info.address_mode_w = wrap.addressMode
        guard let sampler = SDL_CreateGPUSampler(device.handle, &info) else {
          for handle in handles {
            SDL_ReleaseGPUSampler(device.handle, handle)
          }
          throw .sdl()
        }
        handles.append(sampler)
      }
    }
    self.device = device
    self.handles = handles
  }

  deinit {
    for handle in handles {
      SDL_ReleaseGPUSampler(device.handle, handle)
    }
  }

  /// The sampler for a filter and a wrap mode.
  @inline(always)
  subscript(linear linear: Bool, wrap wrap: Wrap) -> OpaquePointer {
    handles[(linear ? Wrap.allCases.count : 0) + wrap.rawValue]
  }

  /// The sampler for the settings of a texture.
  @inline(always)
  subscript(sampling: Sampling) -> OpaquePointer {
    self[linear: sampling.filter != .nearest, wrap: sampling.wrap]
  }
}

extension Wrap {
  var addressMode: SDL_GPUSamplerAddressMode {
    switch self {
    case .clamp: SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE
    case .repeat: SDL_GPU_SAMPLERADDRESSMODE_REPEAT
    case .mirror: SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT
    }
  }
}

/// A GPU buffer that grows. It drops its content when it grows.
final class GrowingBuffer {
  enum Usage {
    case vertex, index, upload
  }

  private let device: Device
  private let usage: Usage
  /// The size of the first buffer in bytes.
  private let initialCapacity: Int
  private(set) var handle: OpaquePointer?
  private(set) var capacity = 0

  init(device: Device, usage: Usage, initialCapacity: Int) {
    self.device = device
    self.usage = usage
    self.initialCapacity = initialCapacity
  }

  deinit {
    release()
  }

  /// Grows the buffer to hold `size` bytes.
  func reserve(_ size: Int) throws(PxlError) {
    guard size > capacity else { return }
    var grown = max(capacity, initialCapacity)
    while grown < size {
      grown *= 2
    }
    guard let bytes = UInt32(exactly: grown) else { throw PxlError("buffer too large") }
    let created: OpaquePointer?
    switch usage {
    case .vertex, .index:
      var info = SDL_GPUBufferCreateInfo()
      info.usage = usage == .vertex ? SDL_GPU_BUFFERUSAGE_VERTEX : SDL_GPU_BUFFERUSAGE_INDEX
      info.size = bytes
      created = SDL_CreateGPUBuffer(device.handle, &info)
    case .upload:
      var info = SDL_GPUTransferBufferCreateInfo()
      info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD
      info.size = bytes
      created = SDL_CreateGPUTransferBuffer(device.handle, &info)
    }
    guard let created else { throw .sdl() }
    release()
    handle = created
    capacity = grown
  }

  private func release() {
    guard let handle else { return }
    switch usage {
    case .vertex, .index: SDL_ReleaseGPUBuffer(device.handle, handle)
    case .upload: SDL_ReleaseGPUTransferBuffer(device.handle, handle)
    }
  }
}
