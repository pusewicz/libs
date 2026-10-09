# frozen_string_literal: true

# Tasks of the Swift port of pxl.h in swift/. "rake check" does not run them:
# CI has no Swift toolchain.

module PxlSwift
  DIR = "swift"
  SOURCES = %w[swift/Sources swift/Tests swift/Package.swift].freeze
  BENCH_C = File.join(BUILD, "swift", "bench_c#{EXE}")
  BENCH_SWIFT = File.join(DIR, ".build", "release", "pxl-bench#{EXE}")
end

BANNER_ROWS["swift"] = -> { describe("swift") }

namespace :swift do
  desc "Write the shaders and the font of the Swift port from pxl.h"
  task :generate do
    ruby "swift/tools/generate.rb"
  end

  desc "Build and run the tests of the Swift port"
  task :test do
    run("swift", "test", "--package-path", PxlSwift::DIR)
  end

  desc "Format the Swift port"
  task :format do
    run("swift", "format", "format", "--in-place", "--recursive", *PxlSwift::SOURCES)
  end

  desc "Check the format of the Swift port"
  task :lint do
    run("swift", "format", "lint", "--strict", "--recursive", *PxlSwift::SOURCES)
  end

  desc "Build the release programs of the Swift port"
  task :release do
    run("swift", "build", "--package-path", PxlSwift::DIR, "-c", "release")
  end

  desc "Compare the CPU time of pxl.h and of the Swift port (RUNS: 5)"
  task :bench, [:runs] => :release do |_, args|
    cflags, = package_flags("pxl")
    mkdir_p File.dirname(PxlSwift::BENCH_C)
    run(*compiler.shellsplit, "@compile_flags.txt", "-O2", *cflags, "-o", PxlSwift::BENCH_C,
        "swift/Benchmarks/bench.c", *link_flags(compiler, "pxl", console: true), "-lm")
    ruby "swift/tools/bench.rb", PxlSwift::BENCH_C, PxlSwift::BENCH_SWIFT, args.fetch(:runs, "5")
  end

  desc "Compare the size of pxl.h and of the Swift port"
  task size: :release do
    ruby "swift/tools/size.rb"
  end
end
