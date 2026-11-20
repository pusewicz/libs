public import CSDL3

extension Context {
  /// Starts a frame and resets the draw state. Then you can draw.
  public func beginFrame() {
    recorder.begin()
    do {
      try updateCanvas()
    } catch {
      recorder.fail(error)
    }
    var width: Int32 = 0
    var height: Int32 = 0
    if let window, SDL_GetWindowSizeInPixels(window, &width, &height) {
      updateViewport(windowWidth: Int(width), windowHeight: Int(height))
    }
  }

  /// Sends the draws of the frame to the GPU and shows the canvas in the
  /// window.
  ///
  /// - Throws: The first error of a draw or of the GPU.
  public func endFrame() throws(PxlError) {
    assert(recorder.inFrame, "endFrame() without beginFrame()")
    guard let cmd = SDL_AcquireGPUCommandBuffer(device.handle) else {
      recorder.reset()
      throw .sdl()
    }
    var swapchain: OpaquePointer?
    var width: UInt32 = 0
    var height: UInt32 = 0
    if let window, !SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &swapchain, &width, &height)
    {
      let error = PxlError.sdl()
      SDL_CancelGPUCommandBuffer(cmd)
      recorder.reset()
      throw error
    }
    let format =
      swapchain != nil
      ? SDL_GetGPUSwapchainTextureFormat(device.handle, window) : SDL_GPU_TEXTUREFORMAT_INVALID
    var recordError: PxlError?
    do {
      try endFrame(
        into: cmd, target: swapchain, format: format, width: Int(width), height: Int(height))
    } catch {
      recordError = error
    }
    guard SDL_SubmitGPUCommandBuffer(cmd) else { throw .sdl() }
    if let recordError {
      throw recordError
    }
  }

  /// Ends the frame as `endFrame()` does, but records into your command buffer
  /// and shows the canvas in your texture. Use it to draw more after pxl, for
  /// example a debug UI.
  ///
  /// - Parameters:
  ///   - commandBuffer: The command buffer. Submit it yourself, before the next
  ///     `beginFrame()`.
  ///   - target: The texture that shows the canvas, for example a swapchain
  ///     texture. nil: only draw into the canvas.
  ///   - format: The format of `target`.
  ///   - width: The width of `target` in pixels.
  ///   - height: The height of `target` in pixels.
  /// - Throws: The first error of a draw.
  public func endFrame(
    into commandBuffer: OpaquePointer, target: OpaquePointer?, format: SDL_GPUTextureFormat,
    width: Int, height: Int
  ) throws(PxlError) {
    assert(recorder.inFrame, "endFrame() without beginFrame()")
    assert(target == nil || (width > 0 && height > 0), "the target needs a size")
    stats = Stats()
    record(commandBuffer, target: target, format: format, width: width, height: height)
    recorder.reset()
    if let error = recorder.frameError {
      throw error
    }
  }

  /// Records the frame into a command buffer.
  private func record(
    _ cmd: OpaquePointer, target: OpaquePointer?, format: SDL_GPUTextureFormat, width: Int,
    height: Int
  ) {
    var canvasQuad: UInt32?
    var sharp = false
    if target != nil {
      sharp = !updateViewport(windowWidth: width, windowHeight: height)
      canvasQuad = addCanvasQuad(sharp: sharp)
    }
    stats.vertices = recorder.batch.vertices.count
    stats.indices = recorder.batch.indices.count
    recorder.batch.closeCommands()
    let uploaded = uploadGeometry(cmd)
    if uploaded {
      renderCommands(cmd)
    }
    if let target {
      renderCanvas(
        cmd, target: target, format: format, width: width, height: height,
        firstIndex: uploaded ? canvasQuad : nil, sharp: sharp)
    }
  }

  /// Appends the quad that shows the canvas in the window. It is not part of a
  /// command.
  ///
  /// - Returns: The first index of the quad, or nil.
  private func addCanvasQuad(sharp: Bool) -> UInt32? {
    guard recorder.batch.hasRoom(vertexCount: 4, indexCount: 6) else {
      recorder.fail(PxlError("too many vertices"))
      return nil
    }
    let first = UInt32(recorder.batch.indices.count)
    let r = viewport
    let quad = Quad(
      topLeft: Vec2(r.x, r.y), topRight: Vec2(r.x + r.width, r.y),
      bottomRight: Vec2(r.x + r.width, r.y + r.height), bottomLeft: Vec2(r.x, r.y + r.height))
    recorder.batch.addQuad(
      quad, uv: (.zero, .one), color: .white, overlay: .transparent,
      params: VertexParams(untextured: 0, sharp: sharp ? 255 : 0))
    return first
  }

  /// Uploads the vertices and indices of the frame to the GPU.
  private func uploadGeometry(_ cmd: OpaquePointer) -> Bool {
    guard recorder.batch.vertices.count > 0 else { return true }
    let vertexBytes = recorder.batch.vertices.count * MemoryLayout<BatchVertex>.stride
    let indexBytes = recorder.batch.indices.count * MemoryLayout<UInt32>.stride
    guard let vertexSize = UInt32(exactly: vertexBytes),
      let indexSize = UInt32(exactly: indexBytes),
      UInt32(exactly: vertexBytes + indexBytes) != nil
    else {
      recorder.fail(PxlError("too many vertices"))
      return false
    }
    do {
      try vertexBuffer.reserve(vertexBytes)
      try indexBuffer.reserve(indexBytes)
      try transferBuffer.reserve(vertexBytes + indexBytes)
    } catch {
      recorder.fail(error)
      return false
    }
    guard let mapped = SDL_MapGPUTransferBuffer(device.handle, transferBuffer.handle, true) else {
      recorder.fail(.sdl())
      return false
    }
    recorder.batch.vertices.withUnsafeBytes {
      mapped.copyMemory(from: $0.baseAddress!, byteCount: $0.count)
    }
    recorder.batch.indices.withUnsafeBytes {
      (mapped + vertexBytes).copyMemory(from: $0.baseAddress!, byteCount: $0.count)
    }
    SDL_UnmapGPUTransferBuffer(device.handle, transferBuffer.handle)

    let copy = SDL_BeginGPUCopyPass(cmd)
    var source = SDL_GPUTransferBufferLocation(transfer_buffer: transferBuffer.handle, offset: 0)
    var destination = SDL_GPUBufferRegion(buffer: vertexBuffer.handle, offset: 0, size: vertexSize)
    SDL_UploadToGPUBuffer(copy, &source, &destination, true)
    source.offset = vertexSize
    destination = SDL_GPUBufferRegion(buffer: indexBuffer.handle, offset: 0, size: indexSize)
    SDL_UploadToGPUBuffer(copy, &source, &destination, true)
    SDL_EndGPUCopyPass(copy)
    return true
  }

  /// The GPU state of the current render pass.
  private struct Pass {
    let handle: OpaquePointer
    let target: Texture
    var pipeline: OpaquePointer?
    /// The last draw in the pass. nil: the GPU state is unknown.
    var last: Int?
  }

  /// Records the commands of the frame into render passes.
  private func renderCommands(_ cmd: OpaquePointer) {
    var pass: Pass?
    for index in recorder.batch.commands.indices {
      let command = recorder.batch.commands[index]
      let clearColor: Color? =
        if case .clear(let color) = command.kind { color } else { nil }
      if clearColor != nil || pass?.target !== command.target {
        if let pass {
          SDL_EndGPURenderPass(pass.handle)
        }
        pass = beginPass(
          cmd, texture: command.target.handle, width: command.target.width,
          height: command.target.height, clear: clearColor, cycle: true
        ).map { Pass(handle: $0, target: command.target) }
      }
      if clearColor == nil, command.indexCount > 0, pass != nil {
        renderDraw(cmd, &pass!, index)
      }
    }
    if let pass {
      SDL_EndGPURenderPass(pass.handle)
    }
  }

  /// Records one draw command into the current render pass.
  private func renderDraw(_ cmd: OpaquePointer, _ pass: inout Pass, _ index: Int) {
    let command = recorder.batch.commands[index]
    let key = command.drawKey!
    let full = SDL_Rect(
      x: 0, y: 0, w: Int32(command.target.width), h: Int32(command.target.height))
    var scissor = full
    if let clip = key.clip {
      var area = clip.sdl
      var bounds = full
      guard SDL_GetRectIntersection(&area, &bounds, &scissor) else { return }
    }
    let pipeline: OpaquePointer
    do {
      pipeline = try command.shader!.pipeline(
        blend: key.blend, format: SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        vertexShader: vertexShader)
    } catch {
      recorder.fail(error)
      return
    }
    var last = pass.last.map { recorder.batch.commands[$0] }
    var lastKey = last?.drawKey
    if pipeline != pass.pipeline {
      bindPipeline(
        cmd, pass.handle, pipeline, width: command.target.width, height: command.target.height)
      pass.pipeline = pipeline
      last = nil
      lastKey = nil
    }
    if lastKey == nil || lastKey!.clip != key.clip {
      SDL_SetGPUScissor(pass.handle, &scissor)
    }
    if command.bindingCount > 0 && !(last?.hasSameBindings(as: command) ?? false) {
      var bindings = InlineArray<4, SDL_GPUTextureSamplerBinding>(repeating: .init())
      for slot in 0..<command.bindingCount {
        bindings[slot] = command.bindings[slot]!.sdl
      }
      bindings.span.withUnsafeBufferPointer {
        SDL_BindGPUFragmentSamplers(pass.handle, 0, $0.baseAddress, UInt32(command.bindingCount))
      }
    }
    if command.shader!.hasUniforms && !key.uniforms.isEmpty
      && (lastKey == nil || lastKey!.uniforms != key.uniforms)
    {
      recorder.batch.uniforms[key.uniforms].withUnsafeBytes {
        SDL_PushGPUFragmentUniformData(cmd, 0, $0.baseAddress, UInt32($0.count))
      }
    }
    SDL_DrawGPUIndexedPrimitives(pass.handle, command.indexCount, 1, command.firstIndex, 0, 0)
    stats.drawCalls += 1
    pass.last = index
  }

  /// Starts a render pass, and binds the geometry of the frame.
  private func beginPass(
    _ cmd: OpaquePointer, texture: OpaquePointer, width: Int, height: Int, clear: Color?,
    cycle: Bool
  ) -> OpaquePointer? {
    var info = SDL_GPUColorTargetInfo()
    info.texture = texture
    info.clear_color = clear?.premultipliedFColor ?? SDL_FColor()
    info.load_op = clear != nil ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD
    info.store_op = SDL_GPU_STOREOP_STORE
    info.cycle = cycle && clear != nil
    guard let pass = SDL_BeginGPURenderPass(cmd, &info, 1, nil) else {
      recorder.fail(.sdl())
      return nil
    }
    if let vertices = vertexBuffer.handle {
      var vertexBinding = SDL_GPUBufferBinding(buffer: vertices, offset: 0)
      var indexBinding = SDL_GPUBufferBinding(buffer: indexBuffer.handle, offset: 0)
      SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1)
      SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT)
    }
    var viewport = SDL_GPUViewport()
    viewport.w = Float(width)
    viewport.h = Float(height)
    viewport.max_depth = 1
    SDL_SetGPUViewport(pass, &viewport)
    stats.passes += 1
    return pass
  }

  /// Binds a pipeline and pushes the projection of the target.
  private func bindPipeline(
    _ cmd: OpaquePointer, _ pass: OpaquePointer, _ pipeline: OpaquePointer, width: Int,
    height: Int
  ) {
    SDL_BindGPUGraphicsPipeline(pass, pipeline)
    var projection = SIMD4<Float>(2 / Float(width), -2 / Float(height), -1, 1)
    SDL_PushGPUVertexUniformData(
      cmd, 0, &projection, UInt32(MemoryLayout.size(ofValue: projection)))
  }

  /// Shows the canvas in a target.
  ///
  /// - Parameter firstIndex: The first index of the canvas quad, or nil to only
  ///   clear.
  private func renderCanvas(
    _ cmd: OpaquePointer, target: OpaquePointer, format: SDL_GPUTextureFormat, width: Int,
    height: Int, firstIndex: UInt32?, sharp: Bool
  ) {
    let opaque = Color(r: letterbox.r, g: letterbox.g, b: letterbox.b)
    guard
      let pass = beginPass(
        cmd, texture: target, width: width, height: height, clear: opaque, cycle: false)
    else {
      return
    }
    defer { SDL_EndGPURenderPass(pass) }
    let pipeline: OpaquePointer
    do {
      pipeline = try recorder.defaultShader.pipeline(
        blend: .alpha, format: format, vertexShader: vertexShader)
    } catch {
      recorder.fail(error)
      return
    }
    guard let firstIndex else { return }
    bindPipeline(cmd, pass, pipeline, width: width, height: height)
    var scissor = SDL_Rect(x: 0, y: 0, w: Int32(width), h: Int32(height))
    SDL_SetGPUScissor(pass, &scissor)
    var binding = SDL_GPUTextureSamplerBinding(
      texture: canvas.handle, sampler: recorder.samplers[linear: sharp, wrap: .clamp])
    SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1)
    SDL_DrawGPUIndexedPrimitives(pass, 6, 1, firstIndex, 0, 0)
    stats.drawCalls += 1
  }
}
