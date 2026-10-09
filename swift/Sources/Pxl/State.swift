import CSDL3

extension Context {
  /// Saves the draw state: the transform, target, blend, clip, snap, shader,
  /// shader textures, uniforms and font.
  public func push() {
    recorder.push()
  }

  /// Restores the draw state of the matching `push()`.
  public func pop() {
    recorder.pop()
  }

  /// Runs `body`, then restores the draw state.
  public func withSavedState<Result, Failure>(_ body: () throws(Failure) -> Result)
    throws(Failure) -> Result
  {
    push()
    defer { pop() }
    return try body()
  }

  /// The transform of the next draws. `Transform.identity` removes it.
  public var transform: Transform {
    get { recorder.state.transform }
    set { recorder.state.transform = newValue }
  }

  /// Moves the next draws.
  public func translate(x: Float, y: Float) {
    recorder.state.transform = recorder.state.transform * Transform(tx: x, ty: y)
  }

  /// Turns the next draws clockwise around the origin.
  public func rotate(by radians: Float) {
    let c = SDL_cosf(radians)
    let s = SDL_sinf(radians)
    recorder.state.transform = recorder.state.transform * Transform(a: c, b: s, c: -s, d: c)
  }

  /// Scales the next draws from the origin.
  public func scale(x: Float, y: Float) {
    recorder.state.transform = recorder.state.transform * Transform(a: x, d: y)
  }

  /// The texture that the next draws go into: a render target, or nil for the
  /// canvas.
  public var target: Texture? {
    get { recorder.state.target }
    set {
      precondition(newValue?.isRenderTarget ?? true, "the target must be a render target")
      recorder.state.target = newValue
    }
  }

  /// The width in pixels of the current target.
  public var width: Int {
    recorder.currentTarget.width
  }

  /// The height in pixels of the current target.
  public var height: Int {
    recorder.currentTarget.height
  }

  /// How the next draws mix with the pixels below.
  public var blend: Blend {
    get { recorder.state.blend }
    set { recorder.state.blend = newValue }
  }

  /// The area of the target that the next draws can change, or nil for all.
  /// The transform does not apply to it.
  public var clip: PixelRect? {
    get { recorder.state.clip }
    set { recorder.state.clip = newValue }
  }

  /// Moves sprites, text and polygons to whole pixels. This stops texels that
  /// flicker. It is on at the start of each frame. Turn it off for smooth
  /// motion with `Filter.sharp`.
  public var snap: Bool {
    get { recorder.state.snap }
    set { recorder.state.snap = newValue }
  }

  /// The shader of the next draws. nil: the default shader.
  public var shader: Shader? {
    get { recorder.state.shader }
    set { recorder.state.shader = newValue }
  }

  /// The font of the next text. nil: the built-in font.
  public var font: Font? {
    get { recorder.state.font }
    set { recorder.state.font = newValue }
  }

  /// Selects a texture for a slot of the current shader.
  ///
  /// - Parameters:
  ///   - texture: The texture, or nil for white.
  ///   - slot: The slot, 1 to `Shader.maxTextures - 1`. Slot 0 is the draw
  ///     texture.
  public func setShaderTexture(_ texture: Texture?, slot: Int) {
    precondition((1..<Shader.maxTextures).contains(slot), "the slot must be 1 to 3")
    recorder.state.shaderTextures[slot] = texture
  }

  /// Sets the data of uniform buffer 0 for the next draws. pxl copies it.
  ///
  /// - Parameter value: The data in std140 layout. The size must be a multiple
  ///   of 16 bytes.
  public func setUniforms<T: BitwiseCopyable>(_ value: T) {
    recorder.setUniforms(value)
  }

  /// Removes the uniform data of the next draws.
  public func clearUniforms() {
    recorder.state.uniforms = 0..<0
  }
}
