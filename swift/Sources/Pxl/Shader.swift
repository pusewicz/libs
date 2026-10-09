import CSDL3

/// The code of a shader in one format.
public struct ShaderCode: Sendable {
  /// The code.
  public var bytes: [UInt8]

  /// The entry point. nil: "main", or "main0" for MSL.
  public var entrypoint: String?

  /// Makes the code of a binary format, for example SPIR-V.
  public init(_ bytes: [UInt8], entrypoint: String? = nil) {
    self.bytes = bytes
    self.entrypoint = entrypoint
  }

  /// Makes the code of a text format, for example MSL.
  public init(source: String, entrypoint: String? = nil) {
    self.init(Array(source.utf8), entrypoint: entrypoint)
  }
}

/// The settings of a fragment shader. Give the code in all formats of
/// `Device.shaderFormats`, or in the format of your device.
///
/// The shader gets the inputs of tools/pxl/shaders/sprite.frag. It samples
/// the draw texture in slot 0 and the textures of
/// `Context.setShaderTexture(_:slot:)` in the slots after it. Uniform buffer 0
/// holds the data of `Context.setUniforms(_:)`.
public struct ShaderDescriptor: Sendable {
  /// The SPIR-V code, for Vulkan.
  public var spirv: ShaderCode?
  /// The DXIL code, for Direct3D 12.
  public var dxil: ShaderCode?
  /// The MSL source, for Metal.
  public var msl: ShaderCode?

  /// The number of texture slots, 1 to `Shader.maxTextures`.
  public var textureCount: Int

  /// The shader reads uniform buffer 0.
  public var hasUniforms: Bool

  /// Makes the settings of a fragment shader.
  public init(
    spirv: ShaderCode? = nil, dxil: ShaderCode? = nil, msl: ShaderCode? = nil,
    textureCount: Int = 1, hasUniforms: Bool = false
  ) {
    self.spirv = spirv
    self.dxil = dxil
    self.msl = msl
    self.textureCount = textureCount
    self.hasUniforms = hasUniforms
  }

  /// The default fragment shader.
  static let sprite = ShaderDescriptor(
    spirv: ShaderCode(spriteFragSPIRV), dxil: ShaderCode(spriteFragDXIL),
    msl: ShaderCode(source: spriteFragMSL))

  /// The vertex shader of all draws.
  static let vertex = ShaderDescriptor(
    spirv: ShaderCode(spriteVertSPIRV), dxil: ShaderCode(spriteVertDXIL),
    msl: ShaderCode(source: spriteVertMSL))
}

/// A fragment shader that replaces the default one.
///
/// pxl releases the GPU shader when the last reference goes. A draw keeps its
/// shader until the frame ends.
public final class Shader {
  /// The number of texture slots that a shader can sample.
  public static let maxTextures = 4

  let device: Device
  let handle: OpaquePointer

  /// The number of texture slots.
  public let textureCount: Int

  /// The shader reads uniform buffer 0.
  public let hasUniforms: Bool

  /// The pipelines of the shader, by blend mode and target format.
  private var pipelines: [PipelineKey: OpaquePointer] = [:]

  /// Creates a fragment shader.
  ///
  /// - Parameters:
  ///   - device: The device.
  ///   - descriptor: The settings.
  public convenience init(device: Device, _ descriptor: ShaderDescriptor) throws(PxlError) {
    guard (1...Self.maxTextures).contains(descriptor.textureCount) else {
      throw PxlError("a shader has 1 to \(Self.maxTextures) textures")
    }
    try self.init(
      device: device, stage: SDL_GPU_SHADERSTAGE_FRAGMENT, descriptor: descriptor,
      uniformBuffers: descriptor.hasUniforms ? 1 : 0)
  }

  /// Creates a shader of a stage.
  init(
    device: Device, stage: SDL_GPUShaderStage, descriptor: ShaderDescriptor, uniformBuffers: Int
  ) throws(PxlError) {
    let code: ShaderCode?
    let entrypoint: String
    switch device.shaderFormat {
    case .spirv: (code, entrypoint) = (descriptor.spirv, "main")
    case .dxil: (code, entrypoint) = (descriptor.dxil, "main")
    case .msl: (code, entrypoint) = (descriptor.msl, "main0")
    }
    guard let code, !code.bytes.isEmpty else {
      throw PxlError("no shader code for the format of the device")
    }
    let samplers = stage == SDL_GPU_SHADERSTAGE_FRAGMENT ? descriptor.textureCount : 0
    let handle = code.bytes.withUnsafeBufferPointer { bytes in
      (code.entrypoint ?? entrypoint).withCString { name in
        var info = SDL_GPUShaderCreateInfo()
        info.code_size = bytes.count
        info.code = bytes.baseAddress
        info.entrypoint = name
        info.format = device.shaderFormat.sdl
        info.stage = stage
        info.num_samplers = UInt32(samplers)
        info.num_uniform_buffers = UInt32(uniformBuffers)
        return SDL_CreateGPUShader(device.handle, &info)
      }
    }
    guard let handle else { throw .sdl() }
    self.device = device
    self.handle = handle
    self.textureCount = samplers
    self.hasUniforms = uniformBuffers > 0
  }

  deinit {
    for pipeline in pipelines.values {
      SDL_ReleaseGPUGraphicsPipeline(device.handle, pipeline)
    }
    SDL_ReleaseGPUShader(device.handle, handle)
  }

  /// Gets a pipeline from the cache, or creates it.
  func pipeline(blend: Blend, format: SDL_GPUTextureFormat, vertexShader: Shader)
    throws(PxlError) -> OpaquePointer
  {
    let key = PipelineKey(blend: blend, format: format.rawValue)
    if let pipeline = pipelines[key] {
      return pipeline
    }
    let attributes = [
      SDL_GPUVertexAttribute(
        location: 0, buffer_slot: 0, format: SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
        offset: BatchVertex.offset(of: \.x)),
      SDL_GPUVertexAttribute(
        location: 1, buffer_slot: 0, format: SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
        offset: BatchVertex.offset(of: \.u)),
      SDL_GPUVertexAttribute(
        location: 2, buffer_slot: 0, format: SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,
        offset: BatchVertex.offset(of: \.color)),
      SDL_GPUVertexAttribute(
        location: 3, buffer_slot: 0, format: SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,
        offset: BatchVertex.offset(of: \.overlay)),
      SDL_GPUVertexAttribute(
        location: 4, buffer_slot: 0, format: SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,
        offset: BatchVertex.offset(of: \.params)),
    ]
    var buffer = SDL_GPUVertexBufferDescription()
    buffer.pitch = UInt32(MemoryLayout<BatchVertex>.stride)
    buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX
    var target = SDL_GPUColorTargetDescription()
    target.format = format
    target.blend_state = blend.state

    var info = SDL_GPUGraphicsPipelineCreateInfo()
    info.vertex_shader = vertexShader.handle
    info.fragment_shader = handle
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE
    let pipeline = withUnsafePointer(to: buffer) { buffer in
      withUnsafePointer(to: target) { target in
        attributes.withUnsafeBufferPointer { attributes in
          info.vertex_input_state.vertex_buffer_descriptions = buffer
          info.vertex_input_state.num_vertex_buffers = 1
          info.vertex_input_state.vertex_attributes = attributes.baseAddress
          info.vertex_input_state.num_vertex_attributes = UInt32(attributes.count)
          info.target_info.color_target_descriptions = target
          info.target_info.num_color_targets = 1
          return SDL_CreateGPUGraphicsPipeline(device.handle, &info)
        }
      }
    }
    guard let pipeline else { throw .sdl() }
    pipelines[key] = pipeline
    return pipeline
  }
}

/// The key of a cached pipeline.
private struct PipelineKey: Hashable {
  let blend: Blend
  let format: SDL_GPUTextureFormat.RawValue
}

extension Blend {
  /// The blend state of the blend mode, for premultiplied alpha.
  var state: SDL_GPUColorTargetBlendState {
    var state = SDL_GPUColorTargetBlendState()
    state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE
    state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA
    state.color_blend_op = SDL_GPU_BLENDOP_ADD
    state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE
    state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA
    state.alpha_blend_op = SDL_GPU_BLENDOP_ADD
    state.enable_blend = true
    switch self {
    case .alpha:
      break
    case .add:
      state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE
      state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO
      state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE
    case .multiply:
      state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_DST_COLOR
      state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO
      state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE
    case .replace:
      state.enable_blend = false
    }
    return state
  }
}
