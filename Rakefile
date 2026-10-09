# frozen_string_literal: true

require "rake/clean"

BUILD = "build"
FIXTURES = "#{BUILD}/fixtures/aseprite"
SANITIZE = %w[-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all].freeze
SOURCES = FileList["*.h", "tests/**/*.{c,h}", "examples/**/*.c"]
TIDY_SOURCES = FileList["tests/**/*.c", "examples/**/*.c"]
LLVM = "/opt/homebrew/opt/llvm/bin"

CLEAN.include(BUILD)

# Finds a tool. An environment variable wins, then Homebrew LLVM, then PATH.
def tool(variable, *names)
  return ENV[variable] if ENV[variable]

  names.each do |name|
    llvm = File.join(LLVM, name)
    return llvm if File.executable?(llvm)
    return name if system("command -v #{name} > /dev/null 2>&1")
  end
  abort "#{variable}: none of #{names.join(', ')} found"
end

def compilers
  { "clang" => tool("CLANG", "clang"), "gcc" => tool("GCC", "gcc-16", "gcc-15", "gcc") }
end

# Compiles C files into one program with the flags in compile_flags.txt.
def compile(compiler, sources, output, flags = SANITIZE)
  mkdir_p File.dirname(output)
  sh compiler, "@compile_flags.txt", *flags, "-o", output, *Array(sources)
end

file "#{FIXTURES}/rgba.aseprite" => "tests/aseprite/fixtures.rb" do
  ruby "tests/aseprite/fixtures.rb", FIXTURES
end

desc "Write the test fixtures"
task fixtures: "#{FIXTURES}/rgba.aseprite"

desc "Build and run the tests with clang and gcc"
task test: :fixtures do
  compilers.each do |name, compiler|
    { "test_aseprite" => %w[test_aseprite consumer], "test_options" => %w[test_options] }.each do |program, files|
      output = "#{BUILD}/#{name}/#{program}"
      compile(compiler, files.map { "tests/aseprite/#{_1}.c" }, output)
      sh output, FIXTURES
    end
  end
end

desc "Build the examples with clang and gcc"
task :examples do
  compilers.each do |name, compiler|
    FileList["examples/**/*.c"].each do |source|
      compile(compiler, source, "#{BUILD}/#{name}/#{File.basename(source, '.c')}")
    end
  end
end

desc "Format the sources"
task :format do
  sh tool("CLANG_FORMAT", "clang-format"), "-i", *SOURCES
end

namespace :format do
  desc "Check the format of the sources"
  task :check do
    sh tool("CLANG_FORMAT", "clang-format"), "--dry-run", "--Werror", *SOURCES
  end
end

desc "Run clang-tidy on the files that compile the implementation"
task :tidy do
  sh tool("CLANG_TIDY", "clang-tidy"), "--quiet", *TIDY_SOURCES
end

desc "Fuzz aseprite_load_memory with libFuzzer (clang only)"
task :fuzz, [:seconds] => :fixtures do |_, args|
  seconds = args.fetch(:seconds, "60")
  output = "#{BUILD}/fuzz/fuzz_aseprite"
  corpus = "#{BUILD}/fuzz/corpus"
  compile(compilers.fetch("clang"), "tests/aseprite/fuzz.c", output,
          %w[-g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all])
  mkdir_p corpus
  cp FileList["#{FIXTURES}/*.aseprite"], corpus
  sh output, "-max_total_time=#{seconds}", "-max_len=65536", "-rss_limit_mb=2048", corpus
end

desc "Load every .ase and .aseprite file in the given directories"
task :sweep, [:directories] do |_, args|
  abort "usage: rake sweep[DIR:DIR...]" unless args[:directories]

  directories = args[:directories].split(":")
  files = directories.flat_map { |dir| Dir.glob(File.join(dir, "**", "*.{ase,aseprite}"), File::FNM_DOTMATCH) }
  abort "no files found in #{directories.join(', ')}" if files.empty?
  output = "#{BUILD}/sweep/sweep"
  compile(compilers.fetch("clang"), "tests/aseprite/sweep.c", output)
  sh(output, *files, verbose: false) { |ok, _| abort "sweep: some files did not load" unless ok }
end

desc "Check format, tidy and tests (the definition of done)"
task check: ["format:check", :tidy, :test, :examples]

task default: :check
