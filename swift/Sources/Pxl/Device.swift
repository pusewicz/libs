public import CSDL3

/// A GPU device. Contexts, textures and shaders keep it alive, so it stays
/// until the last of them is gone.
public final class Device {
  /// The shader formats of pxl.
  public static let shaderFormats: SDL_GPUShaderFormat =
    SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL

  /// The SDL device. The device owns it.
  public let handle: OpaquePointer

  /// The shader format that pxl uses on this device.
  let shaderFormat: ShaderFormat

  /// Creates a device for the shader formats of pxl. Initialize the SDL video
  /// subsystem first.
  ///
  /// - Parameters:
  ///   - debug: Turn on the checks of the GPU API.
  ///   - driver: The name of the GPU driver, or nil for the best one.
  public init(debug: Bool = false, driver: String? = nil) throws(PxlError) {
    guard let handle = SDL_CreateGPUDevice(Self.shaderFormats, debug, driver) else {
      throw .sdl()
    }
    guard let format = ShaderFormat(supported: SDL_GetGPUShaderFormats(handle)) else {
      SDL_DestroyGPUDevice(handle)
      throw PxlError("the device takes none of the shader formats of pxl")
    }
    self.handle = handle
    self.shaderFormat = format
  }

  deinit {
    SDL_DestroyGPUDevice(handle)
  }

  /// The name of the GPU driver, for example "metal" or "vulkan".
  public var driver: String {
    String(cString: SDL_GetGPUDeviceDriver(handle))
  }
}

/// A shader format of pxl.
enum ShaderFormat {
  case spirv, dxil, msl

  /// Selects the first format of pxl that a device supports.
  init?(supported: SDL_GPUShaderFormat) {
    if supported & SDL_GPU_SHADERFORMAT_SPIRV != 0 {
      self = .spirv
    } else if supported & SDL_GPU_SHADERFORMAT_DXIL != 0 {
      self = .dxil
    } else if supported & SDL_GPU_SHADERFORMAT_MSL != 0 {
      self = .msl
    } else {
      return nil
    }
  }

  var sdl: SDL_GPUShaderFormat {
    switch self {
    case .spirv: SDL_GPU_SHADERFORMAT_SPIRV
    case .dxil: SDL_GPU_SHADERFORMAT_DXIL
    case .msl: SDL_GPU_SHADERFORMAT_MSL
    }
  }
}
