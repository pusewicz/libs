// The smallest pxl program: a 320 x 180 canvas with a sprite and text.

import CSDL3
import Pxl

/// Makes a texture from text art. Each character is a pixel. Characters that
/// are not in the palette are clear. Line breaks are ignored.
func artTexture(device: Device, width: Int, height: Int, art: String, palette: [Character: Color])
  throws(PxlError) -> Texture
{
  let pixels = art.filter { !$0.isNewline }.map { palette[$0] ?? .transparent }
  return try Texture(device: device, width: width, height: height, pixels: pixels)
}

/// Shows the window until it closes. The context and the device go when this
/// returns, before the window.
func run(window: OpaquePointer) throws(PxlError) {
  let device = try Device(debug: true)
  let pxl = try Context(device: device, window: window, resolution: (320, 180))
  let smiley = try artTexture(
    device: device, width: 8, height: 8,
    art: """
      ..####..
      .######.
      ##o##o##
      ########
      #o####o#
      ##oooo##
      .######.
      ..####..
      """,
    palette: ["#": Color(rgb: 0xffec27), "o": Color(rgb: 0x000000)])

  var event = SDL_Event()
  while true {
    while SDL_PollEvent(&event) {
      if event.type == SDL_EVENT_QUIT.rawValue {
        return
      }
    }
    let t = Float(SDL_GetTicks()) / 1000
    pxl.beginFrame()
    pxl.clear(Color(rgb: 0x1d2b53))
    pxl.drawSprite(smiley, at: [160, 90 + 10 * SDL_sinf(t * 3)], origin: [4, 4], scale: [4, 4])
    pxl.drawText("Hello, pixels!", at: [4, 4], color: .white)
    try pxl.endFrame()
  }
}

guard SDL_Init(SDL_INIT_VIDEO),
  let window = SDL_CreateWindow(
    "pxl: hello", 960, 540, CSDL3_WINDOW_RESIZABLE | CSDL3_WINDOW_HIGH_PIXEL_DENSITY)
else {
  fatalError(String(cString: SDL_GetError()))
}
do {
  try run(window: window)
} catch {
  print(error)
}
SDL_DestroyWindow(window)
SDL_Quit()
