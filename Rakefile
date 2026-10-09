# frozen_string_literal: true

# Environment:
#
#   CC          The C compiler, with flags if needed. Default: cc.
#   CLANG       The clang of "rake fuzz". It needs libFuzzer, which Apple
#               clang does not have. Default: Homebrew LLVM clang, or clang.
#   PKG_CONFIG  The pkg-config program. Default: pkg-config.
#   SANITIZE=0  Build the tests and the examples without sanitizers.
#
# Tasks run in parallel. "rake -j N" runs at most N tasks at a time.

require "monitor"
require "open3"
require "rake/clean"
require "shellwords"

BUILD = "build"
FIXTURES = "#{BUILD}/fixtures/aseprite"
SANITIZE = %w[-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all].freeze
SOURCES = FileList["*.h", "tests/**/*.{c,h}", "examples/**/*.{c,h}"]
TIDY_SOURCES = FileList["tests/**/*.c", "examples/**/*.c"]
LLVM = "/opt/homebrew/opt/llvm/bin"
EXE = RbConfig::CONFIG["EXEEXT"]

# The pkg-config packages of each library.
PACKAGES = { "pxl" => %w[sdl3] }.freeze

# The test programs of each library: program => the C files in tests/<name>/.
TESTS = {
  "aseprite" => { "test_aseprite" => %w[test_aseprite consumer], "test_options" => %w[test_options] },
  "pxl" => { "test_pxl" => %w[test_pxl], "test_options" => %w[test_options] },
}.freeze

# The arguments of the test programs of each library.
TEST_ARGUMENTS = { "aseprite" => [FIXTURES] }.freeze

# run captures the output of the tools, so they do not see the terminal. These
# flags keep their colors.
COMPILER_COLOR = $stdout.tty? ? %w[-fdiagnostics-color=always] : []
TIDY_COLOR = $stdout.tty? ? %w[--use-color] : []

CLEAN.include(BUILD)

MEMO = {}
MEMO_LOCK = Monitor.new
OUTPUT_LOCK = Mutex.new

# Returns the value of a key. Calculates it with the block only once, also
# when tasks run in parallel.
def memoize(key)
  MEMO_LOCK.synchronize { MEMO.fetch(key) { MEMO[key] = yield } }
end

# Runs a command and prints it with its output in one block, so that the
# output of parallel tasks does not mix. Raises an error if the command fails.
def run(*command)
  output, status = Open3.capture2e(*command)
  OUTPUT_LOCK.synchronize do
    rake_output_message(command.join(" ")) if Rake::FileUtilsExt.verbose_flag
    $stdout.write(output)
    $stdout.flush
  end
  raise "Command failed with status (#{status.exitstatus}): #{command.join(' ')}" unless status.success?
end

# Finds a tool. An environment variable wins, then Homebrew LLVM, then PATH.
def tool(variable, *names)
  return ENV[variable] if ENV[variable]

  names.each do |name|
    llvm = File.join(LLVM, name)
    return llvm if File.executable?(llvm)
    return name if which(name)
  end
  abort "#{variable}: none of #{names.join(', ')} found"
end

# Returns the absolute path of a program: `name` if it has a directory, else
# the first match in PATH. Returns nil if there is no program.
def which(name)
  dirs = File.dirname(name) == "." ? ENV.fetch("PATH", "").split(File::PATH_SEPARATOR) : [""]
  paths = dirs.map { _1.empty? ? name : File.join(_1, name) }
  path = paths.product(["", EXE].uniq).map(&:join).find { File.file?(_1) && File.executable?(_1) }
  path && File.expand_path(path)
end

# Returns the C compiler: CC, or cc.
def compiler
  ENV.fetch("CC", "cc")
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
  memoize([:packages, library]) { query_packages(PACKAGES.fetch(library, [])) }
end

# Returns the run paths that a compiler adds to each program.
def default_rpaths(compiler)
  memoize([:rpaths, compiler]) do
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
  run(*compiler.shellsplit, "@compile_flags.txt", *COMPILER_COLOR, *flags, *cflags, "-o", output, *sources,
      *link_flags(compiler, library, console: console), "-lm")
end

def build_flags
  ENV["SANITIZE"] == "0" ? %w[-g -O1] : SANITIZE
end

# Defines a task that compiles C files into a program, and returns its name.
# The name is the path of the program without EXE, with ":" for "/", for
# example "build:tests:aseprite:test_aseprite".
def program_task(sources, output, console: false)
  name = output.delete_suffix(EXE).tr("/", ":")
  task(name) { compile(compiler, sources, output, console: console) }
  name
end

file "#{FIXTURES}/rgba.aseprite" => "tests/aseprite/fixtures.rb" do
  ruby "tests/aseprite/fixtures.rb", FIXTURES
end

desc "Write the test fixtures"
task fixtures: "#{FIXTURES}/rgba.aseprite"

test_builds = []
test_runs = []
TESTS.each do |library, programs|
  programs.each do |program, files|
    output = "#{BUILD}/tests/#{library}/#{program}#{EXE}"
    build = program_task(files.map { "tests/#{library}/#{_1}.c" }, output, console: true)
    test_run = task("test:#{library}:#{program}" => [:fixtures, build]) do
      run(output, *TEST_ARGUMENTS.fetch(library, []))
    end
    test_builds << build
    test_runs << test_run.name
  end
end

desc "Build and run the tests"
multitask test: test_runs

namespace :build do
  desc "Build the tests, but do not run them"
  multitask tests: test_builds
end

example_builds = FileList["examples/**/*.c"].map do |source|
  program_task(source, "#{BUILD}/#{source.delete_suffix('.c')}#{EXE}")
end

desc "Build the examples"
multitask examples: example_builds

desc "Format the sources"
task :format do
  sh tool("CLANG_FORMAT", "clang-format"), "-i", *SOURCES
end

namespace :format do
  desc "Check the format of the sources"
  task :check do
    run tool("CLANG_FORMAT", "clang-format"), "--dry-run", "--Werror", *SOURCES
  end
end

tidy_tasks = TIDY_SOURCES.map do |source|
  task("tidy:#{source}") do
    cflags, = package_flags(library_of(source))
    run tool("CLANG_TIDY", "clang-tidy"), "--quiet", *TIDY_COLOR, *cflags.map { "--extra-arg=#{_1}" }, source
  end.name
end

desc "Run clang-tidy on the files that compile the implementation"
multitask tidy: tidy_tasks

desc "Fuzz aseprite_load_memory with libFuzzer (clang only)"
task :fuzz, [:seconds] => :fixtures do |_, args|
  seconds = args.fetch(:seconds, "60")
  output = "#{BUILD}/fuzz/fuzz_aseprite"
  corpus = "#{BUILD}/fuzz/corpus"
  compile(tool("CLANG", "clang"), "tests/aseprite/fuzz.c", output,
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
  compile(compiler, "tests/aseprite/sweep.c", output)
  sh(output, *files, verbose: false) { |ok, _| abort "sweep: some files did not load" unless ok }
end

desc "Check format, tidy and tests (the definition of done)"
multitask check: ["format:check", :tidy, :test, :examples]

task default: :check

Dir["tools/*/*.rake"].each { |file| load file }
