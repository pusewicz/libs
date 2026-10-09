# frozen_string_literal: true

# Environment:
#
#   CLANG, GCC  The compilers, with flags if needed, for example
#               "clang --target=x86_64-w64-mingw32".
#   PKG_CONFIG  The pkg-config program. Default: pkg-config.
#   SANITIZE=0  Build the tests and the examples without sanitizers.
#   EXE         The suffix of programs, for example ".exe".

require "open3"
require "rake/clean"
require "shellwords"

BUILD = "build"
FIXTURES = "#{BUILD}/fixtures/aseprite"
SANITIZE = %w[-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all].freeze
SOURCES = FileList["*.h", "tests/**/*.{c,h}", "examples/**/*.{c,h}"]
TIDY_SOURCES = FileList["tests/**/*.c", "examples/**/*.c"]
LLVM = "/opt/homebrew/opt/llvm/bin"
EXE = ENV.fetch("EXE", "")

# The pkg-config packages of each library.
PACKAGES = { "pxl" => %w[sdl3] }.freeze

# The test programs of each library: program => the C files in tests/<name>/.
TESTS = {
  "aseprite" => { "test_aseprite" => %w[test_aseprite consumer], "test_options" => %w[test_options] },
  "pxl" => { "test_pxl" => %w[test_pxl], "test_options" => %w[test_options] },
}.freeze

# The arguments of the test programs of each library.
TEST_ARGUMENTS = { "aseprite" => [FIXTURES] }.freeze

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

# Returns the library of a file in tests/<name>/ or examples/<name>/.
def library_of(source)
  source.split("/")[1]
end

# Returns the compile and link flags of pkg-config packages.
def query_packages(packages)
  return [[], []] if packages.empty?

  pkg_config = ENV.fetch("PKG_CONFIG", "pkg-config")
  # Keep system include paths: clang-tidy must see them as system headers.
  env = { "PKG_CONFIG_ALLOW_SYSTEM_CFLAGS" => "1" }
  cflags, cflags_status = Open3.capture2(env, pkg_config, "--cflags", *packages)
  libs, libs_status = Open3.capture2(pkg_config, "--libs", *packages)
  abort "#{pkg_config} did not find #{packages.join(', ')}" unless cflags_status.success? && libs_status.success?
  includes = cflags.shellsplit.map { |flag| flag.sub(/\A-I/, "-isystem") } - ["-isystem/usr/include"]
  [includes, libs.shellsplit]
end

# Returns the compile and link flags of the packages of a library.
def package_flags(library)
  @package_flags ||= {}
  @package_flags[library] ||= query_packages(PACKAGES.fetch(library, []))
end

# Returns the run paths that a compiler adds to each program.
def default_rpaths(compiler)
  @default_rpaths ||= {}
  @default_rpaths[compiler] ||= begin
    output, = Open3.capture2e(*compiler.shellsplit, "-###", "-x", "c", File::NULL, "-o", File::NULL)
    output.scan(/"?-rpath"?\s+"?([^"\s]+)"?/).flatten
  end
end

# Returns the link flags of a library. Drops the run paths that the compiler
# adds already, because the linker warns about duplicates.
#
# console: Keep the console on Windows. SDL links GUI programs.
def link_flags(compiler, library, console:)
  _, libs = package_flags(library)
  libs = libs.reject { |flag| flag == "-mwindows" } if console
  libs.reject do |flag|
    dir = flag[/\A-Wl,-rpath,(.+)\z/, 1]
    dir && default_rpaths(compiler).include?(dir)
  end
end

# Compiles C files into one program with the flags in compile_flags.txt and
# the packages of their library.
def compile(compiler, sources, output, flags = build_flags, console: false)
  sources = Array(sources)
  library = library_of(sources.first)
  cflags, = package_flags(library)
  mkdir_p File.dirname(output)
  sh(*compiler.shellsplit, "@compile_flags.txt", *flags, *cflags, "-o", output, *sources,
     *link_flags(compiler, library, console: console), "-lm")
end

def build_flags
  ENV["SANITIZE"] == "0" ? %w[-g -O1] : SANITIZE
end

# Builds the test programs with a compiler. Returns each program and its
# library.
def build_tests(name, compiler)
  TESTS.flat_map do |library, programs|
    programs.map do |program, files|
      output = "#{BUILD}/#{name}/#{library}/#{program}#{EXE}"
      compile(compiler, files.map { "tests/#{library}/#{_1}.c" }, output, console: true)
      [output, library]
    end
  end
end

file "#{FIXTURES}/rgba.aseprite" => "tests/aseprite/fixtures.rb" do
  ruby "tests/aseprite/fixtures.rb", FIXTURES
end

desc "Write the test fixtures"
task fixtures: "#{FIXTURES}/rgba.aseprite"

desc "Build and run the tests with clang and gcc"
task test: :fixtures do
  compilers.each do |name, compiler|
    build_tests(name, compiler).each { |output, library| sh output, *TEST_ARGUMENTS.fetch(library, []) }
  end
end

namespace :build do
  desc "Build the tests with clang and gcc, but do not run them"
  task tests: :fixtures do
    compilers.each { |name, compiler| build_tests(name, compiler) }
  end
end

desc "Build the examples with clang and gcc"
task :examples do
  compilers.each do |name, compiler|
    FileList["examples/**/*.c"].each do |source|
      compile(compiler, source, "#{BUILD}/#{name}/#{source.delete_suffix('.c')}#{EXE}")
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
  extra = TIDY_SOURCES.map { library_of(_1) }.uniq.flat_map { package_flags(_1).first }.uniq
  sh tool("CLANG_TIDY", "clang-tidy"), "--quiet", *extra.map { "--extra-arg=#{_1}" }, *TIDY_SOURCES
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

# Builds the Docker image in tools/<name>/ and runs a shell command in it, on
# a copy of the sources: the build must not mix with the build of the host.
def docker(name, command)
  image = "libs-#{name}"
  sh "docker", "build", "-q", "-t", image, "-f", "tools/#{name}/Dockerfile", "tools/#{name}"
  copy = "tar -C /src --exclude=./build --exclude=./.git -cf - . | tar -xf -"
  sh "docker", "run", "--rm", "-v", "#{Dir.pwd}:/src:ro", image, "sh", "-c", "#{copy} && #{command}"
end

# The format check stays on the host: clang-format versions format differently.
desc "Run clang-tidy, the tests and the examples on Linux, in Docker"
task :linux do
  docker("linux", "rake tidy test examples")
end

desc "Build the tests and the examples for Windows with MinGW gcc and clang, in Docker"
task :windows do
  docker("windows", "SANITIZE=0 EXE=.exe PKG_CONFIG=x86_64-w64-mingw32-pkg-config " \
                    "GCC=x86_64-w64-mingw32-gcc CLANG='clang --target=x86_64-w64-mingw32 -fuse-ld=lld' " \
                    "rake build:tests examples")
end

desc "Check format, tidy and tests (the definition of done)"
task check: ["format:check", :tidy, :test, :examples]

task default: :check

Dir["tools/*/*.rake"].each { |file| load file }
