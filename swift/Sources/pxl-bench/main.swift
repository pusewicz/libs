// Measures the CPU time of the Swift port for three workloads. The C version is
// swift/Benchmarks/bench.c. Both print the same table.

import CSDL3
import Pxl

let warmupFrames = 30
let timedFrames = 300
let spriteCount = 20000
let shapeCount = 2000
let textCount = 500

/// A named set of draws for one frame.
struct Workload {
  let name: String
  let draw: (Context, Texture, Int) -> Void
}

let workloads = [
  Workload(name: "sprites") { pxl, sprite, frame in
    for i in 0..<spriteCount {
      let scale = 1 + Float(i % 3) * 0.5
      pxl.drawSprite(
        sprite, at: [Float(i % 320), Float(i / 320 % 180)], origin: [8, 8],
        scale: [scale, scale], rotation: i % 4 == 0 ? Float(i + frame) * 0.01 : 0,
        flipX: i % 2 == 0)
    }
  },
  Workload(name: "shapes") { pxl, _, frame in
    for i in 0..<shapeCount {
      let x = Float((i * 7 + frame) % 320)
      let y = Float(i * 13 % 180)
      let color = Color(rgb: UInt32(truncatingIfNeeded: i) &* 2_654_435_761)
      pxl.drawCircle(at: [x, y], radius: Float(4 + i % 8), color: color)
      pxl.drawLine(from: [x, y], to: [x + 40, y + Float(i % 30)], color: color)
      pxl.drawRectLines(Rect(x: x, y: y, width: 24, height: 16), color: color)
    }
  },
  Workload(name: "text") { pxl, _, frame in
    for i in 0..<textCount {
      pxl.drawText(
        "frame \(frame) score \(i * 10)", at: [Float(i * 3 % 300), Float(i * 5 % 170)],
        color: .white)
    }
  },
]

func milliseconds(_ start: UInt64, _ end: UInt64) -> Double {
  Double(end - start) * 1000 / Double(SDL_GetPerformanceFrequency())
}

func median(_ values: [Double]) -> Double {
  values.sorted()[values.count / 2]
}

/// Prints a row of the table.
func printRow(_ columns: String...) {
  let widths = [-8, 10, 10, 6, 9, 9]
  let cells = zip(columns, widths).map { column, width in
    let padding = String(repeating: " ", count: max(abs(width) - column.count, 0))
    return width < 0 ? column + padding : padding + column
  }
  print(cells.joined(separator: " "))
}

/// Formats a number with three decimals.
func format(_ value: Double) -> String {
  let thousandths = Int((value * 1000).rounded())
  let fraction = String(thousandths % 1000)
  return "\(thousandths / 1000).\(String(repeating: "0", count: 3 - fraction.count))\(fraction)"
}

/// Runs a workload. Prints the median times and the work of the last frame.
@MainActor func run(_ workload: Workload, device: Device, pxl: Context, sprite: Texture)
  throws(PxlError)
{
  var record: [Double] = []
  var end: [Double] = []
  for frame in 0..<warmupFrames + timedFrames {
    let t0 = SDL_GetPerformanceCounter()
    pxl.beginFrame()
    pxl.clear(.black)
    workload.draw(pxl, sprite, frame)
    let t1 = SDL_GetPerformanceCounter()
    try pxl.endFrame()
    let t2 = SDL_GetPerformanceCounter()
    SDL_WaitForGPUIdle(device.handle)
    if frame >= warmupFrames {
      record.append(milliseconds(t0, t1))
      end.append(milliseconds(t1, t2))
    }
  }
  let stats = pxl.stats
  printRow(
    workload.name, format(median(record)), format(median(end)), "\(stats.drawCalls)",
    "\(stats.vertices)", "\(stats.indices)")
}

/// Makes a 16 x 16 checkerboard sprite.
func makeSprite(device: Device) throws(PxlError) -> Texture {
  let pixels = (0..<256).map { i in
    (i % 16 + i / 16) % 2 == 1 ? Color(rgb: 0xff004d) : Color(rgb: 0x29adff)
  }
  return try Texture(device: device, width: 16, height: 16, pixels: pixels)
}

/// Runs the workloads that the arguments name, or all.
@MainActor func main() throws(PxlError) {
  let device = try Device()
  let pxl = try Context(device: device, resolution: (320, 180))
  let sprite = try makeSprite(device: device)
  let names = CommandLine.arguments.dropFirst()
  print("Swift, \(device.driver), median of \(timedFrames) frames in ms")
  printRow("workload", "record", "end_frame", "draws", "vertices", "indices")
  for workload in workloads where names.isEmpty || names.contains(workload.name) {
    try run(workload, device: device, pxl: pxl, sprite: sprite)
  }
}

guard SDL_Init(SDL_INIT_VIDEO) else {
  fatalError("SDL_Init: \(String(cString: SDL_GetError()))")
}
do {
  try main()
} catch {
  fatalError("\(error)")
}
SDL_Quit()
