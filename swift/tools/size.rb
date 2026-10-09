# frozen_string_literal: true

# Compares the size of pxl.h and of its Swift port: source lines without the
# generated data, machine code of the library, stripped hello programs and
# compile times. It needs scc, pkg-config and a release build of the Swift
# package. Run it from the repository root.
#
#   ruby swift/tools/size.rb

require "fileutils"
require "open3"
require "shellwords"
require "tmpdir"

module PxlSize
  SWIFT_OBJECTS = "swift/.build/out/Intermediates.noindex/Pxl.build/Release/Pxl-t.build/Objects-normal/arm64"
  SWIFT_HELLO = "swift/.build/release/hello"

  module_function

  def run!(*command)
    output, status = Open3.capture2e(*command)
    abort("#{command.shelljoin}\n#{output}") unless status.success?
    output
  end

  def sdl_flags
    run!("pkg-config", "--cflags", "--libs", "sdl3").shellsplit
  end

  # Writes pxl.h without its generated sections.
  def handwritten_header(path)
    lines = File.readlines("pxl.h")
    first = lines.index { |line| line.include?("// BEGIN GENERATED SHADERS") }
    last = lines.index { |line| line.include?("// END GENERATED FONT") }
    File.write(path, (lines[0...first] + lines[(last + 1)..]).join)
  end

  def lines(dir)
    run!("scc", "--no-cocomo", "--no-complexity", dir)
      .lines.grep(/^(C Header|Swift|Total)/).map { |line| "  #{line.strip.squeeze(' ')}" }
  end

  # Returns the machine code and the size of the loaded sections of object
  # files. Debug info and link-time sections are left out.
  def object_size(objects)
    sections = objects.flat_map do |object|
      run!("size", "-m", object).scan(/Section \((\w+), (\w+)\): (\d+)/)
    end
    loaded = sections.reject { |segment, _, _| %w[__DWARF __LLVM __LD].include?(segment) }
    text = loaded.select { |_, section, _| section == "__text" }.sum { |*, size| size.to_i }
    [text, loaded.sum { |*, size| size.to_i }]
  end

  def seconds
    start = Process.clock_gettime(Process::CLOCK_MONOTONIC)
    yield
    Process.clock_gettime(Process::CLOCK_MONOTONIC) - start
  end

  def report
    Dir.mktmpdir do |dir|
      FileUtils.mkdir_p(%W[#{dir}/c #{dir}/swift])
      handwritten_header("#{dir}/c/pxl.h")
      FileUtils.cp(Dir["swift/Sources/Pxl/*.swift"] - ["swift/Sources/Pxl/Generated.swift"], "#{dir}/swift")
      puts "Source lines without generated data (scc: Lines Blanks Comments Code):"
      puts lines("#{dir}/c"), lines("#{dir}/swift")

      source = "#{dir}/impl.c"
      File.write(source, "#define PXL_IMPLEMENTATION\n#include \"pxl.h\"\n")
      includes = sdl_flags.grep(/^-I/)
      c_time = seconds { run!("cc", "@compile_flags.txt", "-O2", "-c", source, "-o", "#{dir}/impl.o", *includes) }
      c_text, c_total = object_size(["#{dir}/impl.o"])
      swift_text, swift_total = object_size(Dir["#{SWIFT_OBJECTS}/*.o"])
      puts "\nLibrary objects (bytes): machine code / all loaded sections"
      puts "  C     #{c_text} / #{c_total}"
      puts "  Swift #{swift_text} / #{swift_total}"

      run!("cc", "@compile_flags.txt", "-O2", "examples/pxl/hello.c", "-o", "#{dir}/hello_c", *sdl_flags)
      FileUtils.cp(SWIFT_HELLO, "#{dir}/hello_swift")
      run!("strip", "#{dir}/hello_c")
      run!("strip", "#{dir}/hello_swift")
      puts "\nStripped hello (bytes; Swift links its runtime from the OS)"
      puts "  C     #{File.size("#{dir}/hello_c")}"
      puts "  Swift #{File.size("#{dir}/hello_swift")}"

      swift_time = seconds do
        run!("swift", "build", "--package-path", "swift", "-c", "release", "--target", "Pxl",
             "--scratch-path", "#{dir}/swift-build")
      end
      puts "\nClean -O2 / release build of the library (s)"
      puts format("  C     %.2f", c_time)
      puts format("  Swift %.2f (one SwiftPM target, parallel)", swift_time)
    end
  end
end

PxlSize.report if $PROGRAM_NAME == __FILE__
