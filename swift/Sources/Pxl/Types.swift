// Pxl: a 2D renderer for pixel-art games, on top of SDL3 GPU.
// A Swift port of pxl.h 0.1.0.
// SPDX-License-Identifier: Zlib

import CSDL3

/// A color with straight alpha.
public struct Color: Hashable, Sendable, BitwiseCopyable {
  /// The red, green, blue and alpha channels.
  public var r, g, b, a: UInt8

  /// Makes a color from its channels.
  public init(r: UInt8, g: UInt8, b: UInt8, a: UInt8 = 255) {
    self.r = r
    self.g = g
    self.b = b
    self.a = a
  }

  /// Makes an opaque color from 0xRRGGBB.
  public init(rgb: UInt32) {
    self.init(
      r: UInt8(truncatingIfNeeded: rgb >> 16),
      g: UInt8(truncatingIfNeeded: rgb >> 8),
      b: UInt8(truncatingIfNeeded: rgb))
  }

  /// Makes a color from 0xRRGGBBAA.
  public init(rgba: UInt32) {
    self.init(
      r: UInt8(truncatingIfNeeded: rgba >> 24),
      g: UInt8(truncatingIfNeeded: rgba >> 16),
      b: UInt8(truncatingIfNeeded: rgba >> 8),
      a: UInt8(truncatingIfNeeded: rgba))
  }

  /// Opaque white.
  public static let white = Color(r: 255, g: 255, b: 255)

  /// Opaque black.
  public static let black = Color(r: 0, g: 0, b: 0)

  /// No color.
  public static let transparent = Color(r: 0, g: 0, b: 0, a: 0)

  /// The color with premultiplied alpha.
  var premultiplied: Color {
    Color(r: premultiply(r, a), g: premultiply(g, a), b: premultiply(b, a), a: a)
  }

  /// The color as floats with premultiplied alpha.
  var premultipliedFColor: SDL_FColor {
    let alpha = Float(a) / 255
    return SDL_FColor(
      r: Float(r) / 255 * alpha, g: Float(g) / 255 * alpha, b: Float(b) / 255 * alpha, a: alpha)
  }
}

/// Multiplies a color channel by an alpha, rounded.
func premultiply(_ channel: UInt8, _ alpha: UInt8) -> UInt8 {
  UInt8((UInt32(channel) * UInt32(alpha) + 127) / 255)
}

/// A point or a vector.
public typealias Vec2 = SIMD2<Float>

/// A rectangle: the top-left corner and the size.
public struct Rect: Hashable, Sendable {
  /// The top-left corner and the size.
  public var x, y, width, height: Float

  /// Makes a rectangle.
  public init(x: Float, y: Float, width: Float, height: Float) {
    self.x = x
    self.y = y
    self.width = width
    self.height = height
  }
}

/// A rectangle of whole pixels.
public struct PixelRect: Hashable, Sendable {
  /// The top-left corner and the size.
  public var x, y, width, height: Int

  /// Makes a rectangle.
  public init(x: Int, y: Int, width: Int, height: Int) {
    self.x = x
    self.y = y
    self.width = width
    self.height = height
  }

  /// The rectangle for SDL. The values must fit in 32 bits.
  var sdl: SDL_Rect {
    SDL_Rect(x: Int32(x), y: Int32(y), w: Int32(width), h: Int32(height))
  }
}

/// An affine transform. It moves (x, y) to (a x + c y + tx, b x + d y + ty).
public struct Transform: Hashable, Sendable {
  /// The linear part (a, b, c, d) and the translation (tx, ty).
  public var a, b, c, d, tx, ty: Float

  /// Makes a transform. The defaults give the identity.
  public init(a: Float = 1, b: Float = 0, c: Float = 0, d: Float = 1, tx: Float = 0, ty: Float = 0)
  {
    self.a = a
    self.b = b
    self.c = c
    self.d = d
    self.tx = tx
    self.ty = ty
  }

  /// The transform that changes nothing.
  public static let identity = Transform()

  /// The inverse. Use it to convert a canvas position to world space. A
  /// transform without an inverse gives the identity.
  public var inverse: Transform {
    let det = a * d - b * c
    guard det != 0 else { return .identity }
    let inv = 1 / det
    return Transform(
      a: d * inv, b: -b * inv, c: -c * inv, d: a * inv,
      tx: (c * ty - d * tx) * inv, ty: (b * tx - a * ty) * inv)
  }

  /// Combines two transforms: `r` applies first, then `l`.
  public static func * (l: Transform, r: Transform) -> Transform {
    Transform(
      a: l.a * r.a + l.c * r.b,
      b: l.b * r.a + l.d * r.b,
      c: l.a * r.c + l.c * r.d,
      d: l.b * r.c + l.d * r.d,
      tx: l.a * r.tx + l.c * r.ty + l.tx,
      ty: l.b * r.tx + l.d * r.ty + l.ty)
  }

  /// Applies a transform to a point.
  public static func * (t: Transform, p: Vec2) -> Vec2 {
    Vec2(t.a * p.x + t.c * p.y + t.tx, t.b * p.x + t.d * p.y + t.ty)
  }
}

/// How the canvas fills the window.
public enum ScaleMode: Sendable {
  /// Scale by the largest whole number that fits. Keep all pixels square.
  case integer
  /// Scale as large as fits, and keep the aspect ratio.
  case fit
  /// Fill the window.
  case stretch
}

/// How a texture is sampled.
public enum Filter: Sendable {
  /// Hard texel edges. Use it at whole-number scales.
  case nearest
  /// Blend the texels. Pixel art becomes blurry.
  case linear
  /// Hard texels with smooth edges. Use it at any scale and angle.
  case sharp
}

/// What a texture shows outside of its area.
public enum Wrap: Int, Sendable, CaseIterable {
  /// Repeat the edge texels.
  case clamp
  /// Repeat the texture.
  case `repeat`
  /// Repeat the texture, mirrored each time.
  case mirror
}

/// How a draw mixes with the pixels below it.
public enum Blend: Sendable {
  /// Draw over the pixels below.
  case alpha
  /// Add the color. Use it for light.
  case add
  /// Multiply by the color. Use it for shadow.
  case multiply
  /// Replace the pixels below, alpha too.
  case replace
}

/// A vertex for `Context.drawTriangles(_:texture:)`.
public struct Vertex: Sendable {
  /// The position. The transform applies to it.
  public var position: Vec2
  /// The texture coordinates. (0, 0) is the top-left of the texture.
  public var uv: Vec2
  /// Multiplies the texels.
  public var color: Color

  /// Makes a vertex.
  public init(position: Vec2, uv: Vec2 = .zero, color: Color = .white) {
    self.position = position
    self.uv = uv
    self.color = color
  }
}

/// The work of the last frame.
public struct Stats: Hashable, Sendable {
  /// The GPU draw calls.
  public var drawCalls = 0
  /// The render passes.
  public var passes = 0
  /// The vertices that pxl sent to the GPU.
  public var vertices = 0
  /// The indices that pxl sent to the GPU.
  public var indices = 0
}

/// An error of pxl.
public struct PxlError: Error, CustomStringConvertible, Sendable {
  /// Why it failed.
  public let description: String

  init(_ description: String) {
    self.description = description
  }

  /// The error of the SDL function that failed last.
  static func sdl() -> PxlError {
    PxlError(String(cString: SDL_GetError()))
  }
}
