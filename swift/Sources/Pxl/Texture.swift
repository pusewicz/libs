public import CSDL3

/// An image on the GPU. It can also be a render target.
///
/// pxl releases the GPU texture when the last reference goes. A draw keeps
/// its texture until the frame ends.
public final class Texture {
  /// The device of the texture.
  public let device: Device

  /// The SDL texture. The texture owns it.
  public let handle: OpaquePointer

  /// The width in pixels.
  public let width: Int

  /// The height in pixels.
  public let height: Int

  /// How draws that come after sample the texture.
  public var filter: Filter {
    get { sampling.filter }
    set { sampling.filter = newValue }
  }

  /// What draws that come after show outside of the texture.
  public var wrap: Wrap {
    get { sampling.wrap }
    set { sampling.wrap = newValue }
  }

  /// The filter and the wrap mode. Each draw reads them.
  ///
  /// Swift does not check access to it at run time: the check costs more than
  /// the draw. Two accesses cannot overlap. `Texture` is not `Sendable`, so
  /// one thread uses it, and no access calls code that could access it again.
  @exclusivity(unchecked) var sampling: Sampling

  /// pxl can draw into the texture. See `Context.target`.
  public let isRenderTarget: Bool

  /// The pixels of `update(x:y:width:height:pixels:)` have premultiplied
  /// alpha.
  let premultiplied: Bool

  /// Creates a texture.
  ///
  /// - Parameters:
  ///   - device: The device.
  ///   - width: The width in pixels.
  ///   - height: The height in pixels.
  ///   - pixels: `width * height` colors, rows top to bottom. nil:
  ///     transparent.
  ///   - premultiplied: The pixels have premultiplied alpha.
  ///   - renderTarget: pxl can draw into the texture.
  ///   - filter: How draws sample the texture.
  ///   - wrap: What draws show outside of the texture.
  public convenience init(
    device: Device, width: Int, height: Int, pixels: [Color]? = nil,
    premultiplied: Bool = false, renderTarget: Bool = false,
    filter: Filter = .nearest, wrap: Wrap = .clamp
  ) throws(PxlError) {
    try self.init(
      device: device, width: width, height: height, premultiplied: premultiplied,
      renderTarget: renderTarget, filter: filter, wrap: wrap)
    if let pixels {
      precondition(pixels.count == width * height, "pixels must have width * height colors")
      try pixels.span.withUnsafeBytes { bytes throws(PxlError) in
        try upload(
          x: 0, y: 0, width: width, height: height, pixels: bytes.baseAddress,
          pitch: width * 4, premultiplied: premultiplied)
      }
    } else if renderTarget {
      try clear()
    } else {
      try upload(
        x: 0, y: 0, width: width, height: height, pixels: nil, pitch: 0, premultiplied: true)
    }
  }

  /// Creates a texture from an SDL surface of any pixel format.
  ///
  /// - Parameters:
  ///   - device: The device.
  ///   - surface: The surface. The caller keeps it.
  public convenience init(device: Device, surface: UnsafeMutablePointer<SDL_Surface>)
    throws(PxlError)
  {
    let rgba =
      surface.pointee.format == SDL_PIXELFORMAT_RGBA32
      ? surface : SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32)
    guard let rgba else { throw .sdl() }
    defer {
      if rgba != surface { SDL_DestroySurface(rgba) }
    }
    guard SDL_LockSurface(rgba) else { throw .sdl() }
    defer { SDL_UnlockSurface(rgba) }
    let image = rgba.pointee
    try self.init(
      device: device, width: Int(image.w), height: Int(image.h), premultiplied: false,
      renderTarget: false, filter: .nearest, wrap: .clamp)
    try upload(
      x: 0, y: 0, width: width, height: height, pixels: image.pixels, pitch: Int(image.pitch),
      premultiplied: false)
  }

  /// Loads a texture from a PNG or BMP file.
  ///
  /// - Parameters:
  ///   - device: The device.
  ///   - path: The file path.
  public convenience init(device: Device, path: String) throws(PxlError) {
    guard let surface = SDL_LoadSurface(path) else { throw .sdl() }
    defer { SDL_DestroySurface(surface) }
    try self.init(device: device, surface: surface)
  }

  /// Creates a texture with undefined pixels.
  init(
    device: Device, width: Int, height: Int, premultiplied: Bool, renderTarget: Bool,
    filter: Filter, wrap: Wrap
  ) throws(PxlError) {
    guard width > 0, height > 0, let w = UInt32(exactly: width), let h = UInt32(exactly: height)
    else {
      throw PxlError("the texture size must be positive")
    }
    var info = SDL_GPUTextureCreateInfo()
    info.type = SDL_GPU_TEXTURETYPE_2D
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER
    if renderTarget {
      info.usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET
    }
    info.width = w
    info.height = h
    info.layer_count_or_depth = 1
    info.num_levels = 1
    guard let handle = SDL_CreateGPUTexture(device.handle, &info) else { throw .sdl() }
    self.device = device
    self.handle = handle
    self.width = width
    self.height = height
    self.sampling = Sampling(filter: filter, wrap: wrap)
    self.isRenderTarget = renderTarget
    self.premultiplied = premultiplied
  }

  deinit {
    SDL_ReleaseGPUTexture(device.handle, handle)
  }

  /// Replaces an area of the texture. All draws of the current frame see the
  /// new pixels.
  ///
  /// - Parameters:
  ///   - x: The left of the area in pixels.
  ///   - y: The top of the area in pixels.
  ///   - width: The width of the area in pixels.
  ///   - height: The height of the area in pixels.
  ///   - pixels: `width * height` colors, rows top to bottom. The alpha is as
  ///     in the `premultiplied` setting of the texture.
  public func update(x: Int, y: Int, width: Int, height: Int, pixels: [Color]) throws(PxlError) {
    guard x >= 0, y >= 0, width > 0, height > 0, width <= self.width - x,
      height <= self.height - y
    else {
      throw PxlError("the area is outside of the texture")
    }
    precondition(pixels.count == width * height, "pixels must have width * height colors")
    try pixels.span.withUnsafeBytes { bytes throws(PxlError) in
      try upload(
        x: x, y: y, width: width, height: height, pixels: bytes.baseAddress, pitch: width * 4,
        premultiplied: premultiplied)
    }
  }

  /// Copies the texture to memory. Waits for the GPU. Use it after
  /// `Context.endFrame()`, for example for screenshots.
  ///
  /// - Returns: `width * height` colors, rows top to bottom, with
  ///   premultiplied alpha.
  public func read() throws(PxlError) -> [Color] {
    let count = width * height
    guard let size = UInt32(exactly: count * 4) else { throw PxlError("texture too large") }
    var info = SDL_GPUTransferBufferCreateInfo()
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD
    info.size = size
    guard let transfer = SDL_CreateGPUTransferBuffer(device.handle, &info) else { throw .sdl() }
    defer { SDL_ReleaseGPUTransferBuffer(device.handle, transfer) }
    guard let cmd = SDL_AcquireGPUCommandBuffer(device.handle) else { throw .sdl() }

    var region = SDL_GPUTextureRegion()
    region.texture = handle
    region.w = UInt32(width)
    region.h = UInt32(height)
    region.d = 1
    var destination = SDL_GPUTextureTransferInfo()
    destination.transfer_buffer = transfer
    destination.pixels_per_row = UInt32(width)
    destination.rows_per_layer = UInt32(height)
    let copy = SDL_BeginGPUCopyPass(cmd)
    SDL_DownloadFromGPUTexture(copy, &region, &destination)
    SDL_EndGPUCopyPass(copy)

    guard let fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd) else { throw .sdl() }
    defer { SDL_ReleaseGPUFence(device.handle, fence) }
    var fences: OpaquePointer? = fence
    guard SDL_WaitForGPUFences(device.handle, true, &fences, 1),
      let mapped = SDL_MapGPUTransferBuffer(device.handle, transfer, false)
    else {
      throw .sdl()
    }
    defer { SDL_UnmapGPUTransferBuffer(device.handle, transfer) }
    return [Color](unsafeUninitializedCapacity: count) { buffer, initialized in
      UnsafeMutableRawBufferPointer(buffer).copyMemory(
        from: UnsafeRawBufferPointer(start: mapped, count: Int(size)))
      initialized = count
    }
  }

  /// Uploads pixels to an area of the texture now.
  ///
  /// - Parameters:
  ///   - pixels: The pixels, or nil for transparent.
  ///   - pitch: The bytes from one row of `pixels` to the next.
  ///   - premultiplied: The pixels have premultiplied alpha.
  func upload(
    x: Int, y: Int, width w: Int, height h: Int, pixels: UnsafeRawPointer?, pitch: Int,
    premultiplied: Bool
  ) throws(PxlError) {
    let row = w * 4
    guard let size = UInt32(exactly: row * h) else { throw PxlError("texture too large") }
    var info = SDL_GPUTransferBufferCreateInfo()
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD
    info.size = size
    guard let transfer = SDL_CreateGPUTransferBuffer(device.handle, &info) else { throw .sdl() }
    defer { SDL_ReleaseGPUTransferBuffer(device.handle, transfer) }
    guard let mapped = SDL_MapGPUTransferBuffer(device.handle, transfer, false) else {
      throw .sdl()
    }
    if let pixels {
      for j in 0..<h {
        let destination = mapped + j * row
        destination.copyMemory(from: pixels + j * pitch, byteCount: row)
        if !premultiplied {
          let colors = destination.bindMemory(to: Color.self, capacity: w)
          for i in 0..<w {
            colors[i] = colors[i].premultiplied
          }
        }
      }
    } else {
      mapped.initializeMemory(as: UInt8.self, repeating: 0, count: Int(size))
    }
    SDL_UnmapGPUTransferBuffer(device.handle, transfer)

    guard let cmd = SDL_AcquireGPUCommandBuffer(device.handle) else { throw .sdl() }
    var source = SDL_GPUTextureTransferInfo()
    source.transfer_buffer = transfer
    source.pixels_per_row = UInt32(w)
    source.rows_per_layer = UInt32(h)
    var region = SDL_GPUTextureRegion()
    region.texture = handle
    region.x = UInt32(x)
    region.y = UInt32(y)
    region.w = UInt32(w)
    region.h = UInt32(h)
    region.d = 1
    let copy = SDL_BeginGPUCopyPass(cmd)
    SDL_UploadToGPUTexture(copy, &source, &region, false)
    SDL_EndGPUCopyPass(copy)
    guard SDL_SubmitGPUCommandBuffer(cmd) else { throw .sdl() }
  }

  /// Clears the texture to transparent now. It must be a render target.
  func clear() throws(PxlError) {
    guard let cmd = SDL_AcquireGPUCommandBuffer(device.handle) else { throw .sdl() }
    var target = SDL_GPUColorTargetInfo()
    target.texture = handle
    target.load_op = SDL_GPU_LOADOP_CLEAR
    target.store_op = SDL_GPU_STOREOP_STORE
    let pass = SDL_BeginGPURenderPass(cmd, &target, 1, nil)
    if let pass {
      SDL_EndGPURenderPass(pass)
    }
    guard SDL_SubmitGPUCommandBuffer(cmd), pass != nil else { throw .sdl() }
  }
}

/// How draws sample a texture.
struct Sampling: Hashable {
  var filter: Filter
  var wrap: Wrap
}
