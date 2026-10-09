import CSDL3

/// A renderer for one window. It draws sprites, shapes and text into a small
/// canvas, then scales the canvas to the window without blur.
///
/// Draw each frame between `beginFrame()` and `endFrame()`. pxl records the
/// draws and sends them to the GPU in `endFrame()`. Positions are in pixels of
/// the current target: (0, 0) is the top-left corner and y points down. Angles
/// are in radians and turn clockwise.
public final class Context {
  /// The device.
  public let device: Device

  /// The window, or nil to render offscreen. pxl claims it for the device.
  public let window: OpaquePointer?

  /// The canvas. It is the default target. pxl scales it to the window. It
  /// changes when the size changes.
  public var canvas: Texture {
    recorder.canvas
  }

  /// The canvas size in pixels. nil: the window size in pixels. A change
  /// starts at the next frame.
  public var resolution: (width: Int, height: Int)?

  /// How the canvas fills the window.
  public var scaleMode: ScaleMode

  /// The color around the canvas. pxl ignores the alpha.
  public var letterbox: Color

  /// The area of the window that shows the canvas, in window pixels.
  public private(set) var viewport: Rect

  /// The work of the last frame.
  public internal(set) var stats = Stats()

  /// The draw state and the draws of the frame. Each draw changes it.
  ///
  /// Swift does not check access to it at run time: the check costs more than
  /// a sprite. Two accesses cannot overlap. `Context` is not `Sendable`, so one
  /// thread uses it, and no method of `Recorder` calls code that could access
  /// it again.
  @exclusivity(unchecked) var recorder: Recorder
  let vertexShader: Shader
  let vertexBuffer: GrowingBuffer
  let indexBuffer: GrowingBuffer
  let transferBuffer: GrowingBuffer
  private var claimedWindow = false
  /// Window pixels for each window coordinate.
  private var pixelDensity: Float = 1

  /// Creates a context.
  ///
  /// - Parameters:
  ///   - device: The device.
  ///   - window: The window. pxl claims it for the device. nil: render
  ///     offscreen.
  ///   - resolution: The canvas size in pixels. nil: the window size in pixels.
  ///   - scaleMode: How the canvas fills the window.
  ///   - letterbox: The color around the canvas. pxl ignores the alpha.
  public init(
    device: Device, window: OpaquePointer? = nil, resolution: (width: Int, height: Int)? = nil,
    scaleMode: ScaleMode = .integer, letterbox: Color = .black
  ) throws(PxlError) {
    if window == nil {
      guard let resolution, resolution.width > 0, resolution.height > 0 else {
        throw PxlError("a context without a window needs a canvas size")
      }
    }
    let size = try Self.canvasSize(window: window, resolution: resolution)
    self.device = device
    self.window = window
    self.resolution = resolution
    self.scaleMode = scaleMode
    self.letterbox = letterbox
    vertexShader = try Shader(
      device: device, stage: SDL_GPU_SHADERSTAGE_VERTEX, descriptor: .vertex, uniformBuffers: 1)
    let canvas = try Texture(
      device: device, width: max(size.width, 1), height: max(size.height, 1), renderTarget: true)
    recorder = Recorder(
      white: try Texture(device: device, width: 1, height: 1, pixels: [.white]),
      samplers: try Samplers(device: device), defaultShader: try Shader(device: device, .sprite),
      defaultFont: try Font(builtInFor: device), canvas: canvas)
    viewport = Rect(x: 0, y: 0, width: Float(canvas.width), height: Float(canvas.height))
    let vertexSize = Self.minCapacity * MemoryLayout<BatchVertex>.stride
    vertexBuffer = GrowingBuffer(device: device, usage: .vertex, initialCapacity: vertexSize)
    indexBuffer = GrowingBuffer(device: device, usage: .index, initialCapacity: vertexSize)
    transferBuffer = GrowingBuffer(
      device: device, usage: .upload, initialCapacity: Self.minCapacity * 64)
    if let window {
      guard SDL_ClaimWindowForGPUDevice(device.handle, window) else { throw .sdl() }
      claimedWindow = true
    }
  }

  deinit {
    assert(!recorder.inFrame, "a context must not be destroyed during a frame")
    SDL_WaitForGPUIdle(device.handle)
    if claimedWindow, let window {
      SDL_ReleaseWindowFromGPUDevice(device.handle, window)
    }
  }

  /// The size of the first GPU buffers, in vertices.
  static let minCapacity = 256

  /// Converts a window position to a canvas position. Use it for the mouse.
  ///
  /// - Parameter point: A position in window coordinates, as in SDL events.
  /// - Returns: The position in canvas pixels. It can be outside of the canvas.
  public func windowToCanvas(_ point: Vec2) -> Vec2 {
    guard viewport.width > 0, viewport.height > 0 else { return .zero }
    let p = point * pixelDensity
    return Vec2(
      (p.x - viewport.x) * Float(canvas.width) / viewport.width,
      (p.y - viewport.y) * Float(canvas.height) / viewport.height)
  }

  /// Gets the canvas size for a resolution. A minimized window gives 0.
  private static func canvasSize(window: OpaquePointer?, resolution: (width: Int, height: Int)?)
    throws(PxlError) -> (width: Int, height: Int)
  {
    if let resolution, resolution.width > 0, resolution.height > 0 {
      return resolution
    }
    var width: Int32 = 0
    var height: Int32 = 0
    if let window, !SDL_GetWindowSizeInPixels(window, &width, &height) {
      throw .sdl()
    }
    return (Int(width), Int(height))
  }

  /// Makes the canvas match the requested size or the window size.
  func updateCanvas() throws(PxlError) {
    let size = try Self.canvasSize(window: window, resolution: resolution)
    guard size.width > 0, size.height > 0 else {
      return  // A minimized window has no size. Keep the canvas.
    }
    guard size != (canvas.width, canvas.height) else { return }
    recorder.canvas = try Texture(
      device: device, width: size.width, height: size.height, renderTarget: true)
  }

  /// Updates the viewport from the size of the window in pixels.
  ///
  /// - Returns: true if each canvas pixel covers whole window pixels.
  @discardableResult
  func updateViewport(windowWidth: Int, windowHeight: Int) -> Bool {
    let (rect, exact) = fit(
      canvasWidth: canvas.width, canvasHeight: canvas.height, windowWidth: windowWidth,
      windowHeight: windowHeight, mode: scaleMode)
    viewport = rect
    var width: Int32 = 0
    var height: Int32 = 0
    if let window, SDL_GetWindowSize(window, &width, &height), width > 0 {
      pixelDensity = Float(windowWidth) / Float(width)
    }
    return exact
  }
}

/// Computes where the canvas goes in the window.
///
/// - Returns: The area, and true if each canvas pixel covers whole window
///   pixels.
func fit(canvasWidth: Int, canvasHeight: Int, windowWidth: Int, windowHeight: Int, mode: ScaleMode)
  -> (rect: Rect, exact: Bool)
{
  let cw = Float(canvasWidth)
  let ch = Float(canvasHeight)
  let ww = Float(windowWidth)
  let wh = Float(windowHeight)
  if mode == .stretch {
    let exact =
      windowWidth % canvasWidth == 0 && windowHeight % canvasHeight == 0
      && windowWidth / canvasWidth == windowHeight / canvasHeight
    return (Rect(x: 0, y: 0, width: ww, height: wh), exact)
  }
  var scale = Float.minimum(ww / cw, wh / ch)
  if mode == .integer && scale >= 1 {
    scale.round(.down)
  }
  let w = roundHalfUp(cw * scale)
  let h = roundHalfUp(ch * scale)
  let rect = Rect(
    x: ((ww - w) / 2).rounded(.down), y: ((wh - h) / 2).rounded(.down), width: w, height: h)
  return (rect, scale >= 1 && scale == scale.rounded(.down))
}

/// Rounds to the nearest whole number, halves up.
@inline(always)
func roundHalfUp(_ value: Float) -> Float {
  (value + 0.5).rounded(.down)
}
