import CSDL3

/// A vertex as the GPU reads it.
struct BatchVertex {
  var x, y, u, v: Float
  var color: Color
  var overlay: Color
  /// x = 255 ignores the texture. y = 255 samples sharp.
  var params: SIMD4<UInt8>

  init(_ position: Vec2, _ uv: Vec2, color: Color, overlay: Color, params: SIMD4<UInt8>) {
    self.x = position.x
    self.y = position.y
    self.u = uv.x
    self.v = uv.y
    self.color = color
    self.overlay = overlay
    self.params = params
  }

  /// The params of the vertices of a texture, or of plain color.
  static func params(_ sampling: Sampling?) -> SIMD4<UInt8> {
    guard let sampling else { return [255, 0, 0, 0] }
    return [0, sampling.filter == .sharp ? 255 : 0, 0, 0]
  }

  /// The offset of a field in bytes.
  static func offset(of field: PartialKeyPath<BatchVertex>) -> UInt32 {
    UInt32(MemoryLayout<BatchVertex>.offset(of: field)!)
  }
}

/// A texture and the sampler of a draw.
struct Binding {
  let texture: Texture
  let sampler: OpaquePointer

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
  ///   - sampling: The settings of `texture`.
  ///   - shaderTextures: The textures of the shader slots.
  ///   - samplers: The samplers of the context.
  ///   - white: The texture of empty shader slots.
  ///   - indexCount: The number of indices of the draw.
  /// - Returns: false if the draw needs a new command.
  mutating func join(
    _ key: DrawKey, texture: Texture?, sampling: Sampling?, shaderTextures: [4 of Texture?],
    samplers: Samplers, white: Texture, indexCount: UInt32
  ) -> Bool {
    guard case .draw(let joined) = kind, joined == key else { return false }
    for slot in 1..<bindingCount {
      let bound = shaderTextures[slot] ?? white
      if !bindings[slot]!.matches(bound, samplers[bound.sampling]) {
        return false
      }
    }
    if let texture, let sampling, !bindings[0]!.matches(texture, samplers[sampling]) {
      return false
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
struct Batch {
  var vertices: [BatchVertex] = []
  var indices: [UInt32] = []
  var commands: [Command] = []
  /// The uniform data of the frame. Each draw uses a range of it.
  var uniforms: [UInt8] = []

  /// Returns true if `vertexCount` more vertices and `indexCount` more indices
  /// fit in 32-bit indices.
  func hasRoom(vertexCount: Int, indexCount: Int) -> Bool {
    vertices.count + vertexCount <= UInt32.max && indices.count + indexCount <= UInt32.max
  }

  /// Appends a quad.
  ///
  /// - Parameters:
  ///   - corners: The corners: top-left, top-right, bottom-right, bottom-left of
  ///     the texture area.
  ///   - uv: The top-left and bottom-right of the texture area.
  mutating func addQuad(
    _ corners: [4 of Vec2], uv: (Vec2, Vec2), color: Color, overlay: Color,
    params: SIMD4<UInt8>
  ) {
    let base = UInt32(vertices.count)
    let (uv0, uv1) = uv
    vertices.append(addingCapacity: 4) { output in
      output.append(BatchVertex(corners[0], uv0, color: color, overlay: overlay, params: params))
      output.append(
        BatchVertex(corners[1], Vec2(uv1.x, uv0.y), color: color, overlay: overlay, params: params))
      output.append(BatchVertex(corners[2], uv1, color: color, overlay: overlay, params: params))
      output.append(
        BatchVertex(corners[3], Vec2(uv0.x, uv1.y), color: color, overlay: overlay, params: params))
    }
    indices.append(addingCapacity: 6) { output in
      for offset: UInt32 in [0, 1, 2, 0, 2, 3] {
        output.append(base + offset)
      }
    }
  }

  /// Appends a vertex, and an index for it.
  mutating func addVertex(_ vertex: BatchVertex) {
    indices.append(UInt32(vertices.count))
    vertices.append(vertex)
  }

  /// Drops the draws, and keeps the memory.
  mutating func removeAll() {
    vertices.removeAll(keepingCapacity: true)
    indices.removeAll(keepingCapacity: true)
    commands.removeAll(keepingCapacity: true)
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
  subscript(linear linear: Bool, wrap wrap: Wrap) -> OpaquePointer {
    handles[(linear ? Wrap.allCases.count : 0) + wrap.rawValue]
  }

  /// The sampler for the settings of a texture.
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
