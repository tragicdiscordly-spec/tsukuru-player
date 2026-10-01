# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compatibility for games written for RPG Maker's Windows player (XP / VX / VX Ace), loaded before the game's own
# scripts (the launcher sets MKXP_PRELOAD to this file).
#
# * Zlib is part of RPG Maker's Ruby; here it has to be loaded.
# * TEMP, TMP, USERPROFILE and USERNAME are set, as on Windows.
# * XP games: text[5] gives a byte number and String#each walks lines, as in Ruby 1.8.
# * Message box texts also go to the game's output.
# * dispose on an object whose engine part was never created is ignored, as RPG Maker did.
# * Font.exist? says yes (fonts cannot be installed here).
# * Thread.critical (Ruby 1.8) does nothing.
# * Key state questions (GetAsyncKeyState) are answered from the console's keyboard and gamepad.
# * Text files: on Windows a file opened in text mode (no "b") gives "\n" for every CRLF. Games with line-based readers (JSON, INI,
#   text tables) depend on it, so text-mode reads here do the same.
# * DL (the old Ruby library for raw memory, used by a few key-reading scripts) has a small stand-in with a byte buffer.
# * Win32API is the class games use to call Windows DLLs (user32, kernel32, steam_api, ...). There is no Windows here,
#   so a call does nothing and answers 0, which is what most games expect when an optional feature (Steam
#   achievements, window tricks, key state) is not available. A few harmless functions are answered properly. The
#   first call of every function is written to the game's output, so it is visible what a game asked for.
begin
  require 'zlib'
rescue LoadError
end

# Windows environment variables games use for scratch files and user names. TEMP points to a folder of its own.
begin
  home = ENV['HOME'] || '.'
  temp = File.join(home, 'tmp')
  Dir.mkdir(temp) unless File.directory?(temp)
  ENV['TEMP'] ||= temp
  ENV['TMP'] ||= temp
  ENV['USERPROFILE'] ||= home
  ENV['USERNAME'] ||= 'Player'
rescue StandardError
end

# Games such as Pokemon Essentials replace the Input module with one that asks Windows which keys are down
# (GetAsyncKeyState), so the gamepad would never reach them. The engine's own Input methods are kept under other
# names here, before the game replaces them, and those key questions are answered from them (see Win32API below).
module Input
  class << self
    unless method_defined?(:__native_update) # this file can be loaded more than once (reset)
      alias_method :__native_update, :update
      alias_method :__native_press?, :press?
    end
  end
end

# RPG Maker XP ran Ruby 1.8, where text[5] is the number of the byte at 5 (newer Ruby gives a one-letter string) and
# a string could be walked line by line with each. XP games read their binary data files that way, so XP games get
# the old behaviour. (XP: no RGSS_VERSION constant and no RPG::BGM class.)
if !defined?(RGSS_VERSION) && !(defined?(RPG) && RPG.const_defined?(:BGM))
  # (text[5] giving the byte number is done inside the engine, see the mkxp-z patch)
  class String
    def each(*args, &block)
      each_line(*args, &block)
    end
  end

  # Ruby 1.8's Marshal.load read single bytes with getc; newer Ruby asks for getbyte. Pokemon Essentials' StringInput
  # is an IO subclass with its own getc and read but no getbyte, so the inherited one fails ("uninitialized stream").
  class IO
    unless method_defined?(:__rgss_getbyte)
      alias_method :__rgss_getbyte, :getbyte

      def getbyte
        __rgss_getbyte
      rescue IOError
        c = getc
        c.is_a?(String) ? c.ord : c
      end
    end
  end
end

# Message boxes (print / p, and msgbox in VX Ace) cannot be shown on the console; their text also goes to the game's
# output, so the launcher can show it.
module Kernel
  [:print, :p, :msgbox, :msgbox_p].each do |name|
    next unless private_method_defined?(name) || method_defined?(name) || respond_to?(name, true)
    original = "__rgss_box_#{name}"
    next if private_method_defined?(original) || method_defined?(original)
    alias_method original, name rescue next
    define_method(name) do |*args|
      STDERR.puts "message box: " + args.map { |a| name.to_s.end_with?("p") ? a.inspect : a.to_s }.join(" ")
      send(original, *args)
    end
    module_function name
  end
end

# Windows turns CRLF into LF when a file is read in text mode. Ruby on Linux does not, so a game's own parser (made for
# files with CRLF, tested only on Windows) meets "\r" it never expected. Reading in text mode ("rt") gives the same
# conversion. Binary reads ("rb") and writing stay as they are.
module RgssTextMode
  def self.args(args, opts)
    return args if opts.key?(:mode) || opts.key?(:binmode) || opts.key?(:newline) || opts.key?(:textmode)
    mode = args[1]
    if mode.nil?
      args = args.dup
      args[1] = "rt"
    elsif mode.is_a?(String) && mode.start_with?("r") && mode[/\A[^:]*/] !~ /[bt]/
      args = args.dup
      args[1] = mode.sub(/\Ar\+?/) { |m| m + "t" }
    end
    args
  end

  def self.read_opts(opts)
    return opts if opts.key?(:mode) || opts.key?(:binmode) || opts.key?(:newline) || opts.key?(:textmode)
    opts.merge(mode: "rt")
  end
end

class File
  class << self
    unless method_defined?(:__rgss_open)
      alias_method :__rgss_open, :open
      alias_method :__rgss_new, :new

      def open(*args, **opts, &block)
        __rgss_open(*RgssTextMode.args(args, opts), **opts, &block)
      end

      def new(*args, **opts, &block)
        __rgss_new(*RgssTextMode.args(args, opts), **opts, &block)
      end
    end
  end
end

class IO
  class << self
    unless method_defined?(:__rgss_read)
      alias_method :__rgss_read, :read
      alias_method :__rgss_readlines, :readlines
      alias_method :__rgss_foreach, :foreach

      # whole-file reads only (a length or an offset means the caller is reading raw bytes)
      def read(name, *args, **opts)
        opts = RgssTextMode.read_opts(opts) if args.empty? && name.is_a?(String)
        __rgss_read(name, *args, **opts)
      end

      def readlines(name, *args, **opts)
        opts = RgssTextMode.read_opts(opts) if name.is_a?(String)
        __rgss_readlines(name, *args, **opts)
      end

      def foreach(name, *args, **opts, &block)
        opts = RgssTextMode.read_opts(opts) if name.is_a?(String)
        __rgss_foreach(name, *args, **opts, &block)
      end
    end
  end
end

module Kernel
  unless private_method_defined?(:__rgss_kernel_open)
    alias_method :__rgss_kernel_open, :open

    def open(name, *args, **opts, &block)
      if name.is_a?(String) && !name.start_with?("|")
        File.open(name, *args, **opts, &block)
      else
        __rgss_kernel_open(name, *args, **opts, &block)
      end
    end
    module_function :open
  end
end

# A game class built on Sprite / Plane / Window that never calls super in initialize (Pokemon Essentials' LargePlane)
# has no engine object behind it. RPG Maker's player ignored dispose on such an object; mkxp-z raises "No instance
# data for variable". Make dispose ignore it too.
%w[Sprite Plane Window Viewport Tilemap Bitmap].each do |name|
  next unless Object.const_defined?(name)
  klass = Object.const_get(name)
  next unless klass.method_defined?(:dispose) && !klass.method_defined?(:__rgss_dispose)
  klass.class_eval do
    alias_method :__rgss_dispose, :dispose

    def dispose(*args)
      __rgss_dispose(*args)
    rescue Exception => e # mkxp-z's MKXPError is not a StandardError
      raise unless e.message.include?("No instance data")
    end
  end
end

# Some games check whether their fonts are installed in Windows and, if not, try to copy them into the Windows
# fonts folder. That cannot work here (it would only litter the game folder), and text in a font that is not found is
# drawn with the built-in font, so every font counts as present.
class Font
  class << self
    def exist?(name)
      true
    end
  end
end

# Thread.critical was removed from Ruby after 1.8; games use it to guard a few lines of code. With a single game thread
# there is nothing to guard against.
class Thread
  class << self
    def critical
      @critical ||= false
    end

    def critical=(value)
      @critical = value
    end
  end
end

# A few scripts (Hime's AllKey, used by LonaRPG) keep the keyboard state in DL::CPtr memory and hand its address to
# GetKeyboardState. DL does not exist in Ruby 3, so this is a byte buffer that has an "address".
unless defined?(DL)
  module DL
    @next_address = 0x10000
    @buffers = {}

    class << self
      attr_reader :buffers

      def malloc(size)
        address = @next_address
        @next_address += (size.to_i + 15) & ~15
        address
      end

      def free(address)
        @buffers.delete(address)
      end
    end

    class CPtr
      attr_reader :size

      def initialize(address = 0, size = 0)
        @address = address.to_i
        @size = size.to_i
        @bytes = Array.new(@size, 0)
        DL.buffers[@address] = self
      end

      def to_i
        @address
      end
      alias_method :to_int, :to_i

      def [](index, length = nil)
        return @bytes[index] if length.nil?
        @bytes[index, length].pack('C*')
      end

      def []=(index, value)
        @bytes[index] = value.to_i & 0xff
      end

      def fill(bytes)
        bytes.each_with_index { |b, i| @bytes[i] = b if i < @size }
      end

      def to_s(length = @size)
        @bytes[0, length].pack('C*')
      end
    end
  end
end

class Win32API
  @@seen = {}

  def initialize(dll, func, import = nil, export = nil)
    @dll = dll.to_s.downcase.sub(/\.dll\z/, '')
    @func = func.to_s
    @export = export.to_s.downcase
  end

  # Windows virtual-key codes the way Pokemon Essentials binds them -> the engine's buttons
  VK_BUTTONS = {
    0x28 => Input::DOWN, 0x25 => Input::LEFT, 0x27 => Input::RIGHT, 0x26 => Input::UP,
    0x5A => Input::A, 0x10 => Input::A, 0x58 => Input::B, 0x1B => Input::B,
    0x43 => Input::C, 0x0D => Input::C, 0x20 => Input::C,
    0x41 => Input::X, 0x53 => Input::Y, 0x44 => Input::Z,
    0x51 => Input::L, 0x21 => Input::L, 0x57 => Input::R, 0x22 => Input::R
  }
  @@input_frame = -1

  # GetClientRect / GetWindowRect: the rectangle of the game picture
  def self.fill_rect(buffer)
    return unless buffer.is_a?(String) && buffer.bytesize >= 16
    w = (Graphics.width rescue 640)
    h = (Graphics.height rescue 480)
    buffer[0, 16] = [0, 0, w, h].pack('l4')
  end

  # Development aid: MKXP_DEBUG_KEYS=frame:vk,frame:vk (e.g. 600:0x43) holds that key for 6 frames from that frame.
  DEBUG_KEYS = (ENV['MKXP_DEBUG_KEYS'] || '').split(',').map { |e| f, k = e.split(':'); [f.to_i, Integer(k)] }

  def self.key_down?(vk)
    unless DEBUG_KEYS.empty?
      n = Graphics.frame_count
      return true if DEBUG_KEYS.any? { |f, k| k == vk && n >= f && n < f + 6 }
    end
    button = VK_BUTTONS[vk]
    return false unless button
    frame = Graphics.frame_count
    if frame != @@input_frame
      @@input_frame = frame
      Input.__native_update
    end
    Input.__native_press?(button)
  end

  def call(*args)
    case @func
    when /\AGet(Async)?KeyState/
      return Win32API.key_down?(args[0].to_i) ? 0x8000 : 0
    when /\AGetForegroundWindow/, /\AGetWindowThreadProcessId/, /\AFindWindow/, /\AGetActiveWindow/
      return 1 # the game's one window (handle 1), in front
    when /\AGetSystemMetrics/
      return { 0 => 1920, 1 => 1080, 16 => 1920, 17 => 1080 }.fetch(args[0].to_i, 0) # screen size
    when /\AGetClientRect/, /\AGetWindowRect/
      Win32API.fill_rect(args[1])
      return 1
    when /\AGetCommandLine/
      return 'Game.exe' # the command line of the game (a string for a "P" result)
    when /\ASystemParametersInfo/
      if args[0].to_i == 0x30 && args[2].is_a?(String) && args[2].bytesize >= 16 # SPI_GETWORKAREA
        args[2][0, 16] = [0, 0, 1920, 1040].pack('l4')
        return 1
      end
    when /\AGetKeyboardState/
      pointer = DL.buffers[args[0].to_i] if defined?(DL) && DL.respond_to?(:buffers)
      if pointer
        pointer.fill((0...256).map { |vk| Win32API.key_down?(vk) ? 0x80 : 0 })
        return 1
      end
    when /\AGetKeyboardLayout/
      return 0x0409 # English (United States)
    when /\AMapVirtualKey/
      return args[0].to_i if args[1].to_i == 2 # the key's own code as a character
    when /\AAdjustWindowRect/, /\AUpdateWindow/, /\ASetWindowPos/, /\AShowWindow/
      return 1
    when /\AReg/
      return 2 # registry: "key not found" (ERROR_FILE_NOT_FOUND)
    end
    key = "#{@dll}.#{@func}"
    unless @@seen[key]
      @@seen[key] = true
      STDERR.puts "Win32API stand-in: #{key} is not available (returns 0)"
    end
    case @func
    when /\AGetPrivateProfileInt/
      args[2].to_i # the default value
    when /\A(GetTickCount|timeGetTime)/
      (Process.clock_gettime(Process::CLOCK_MONOTONIC) * 1000).to_i & 0xffffffff
    when /\AGetCurrentProcessId|GetCurrentThreadId/
      1
    else
      0
    end
  end
end

# How deep can the game's Ruby code recurse here? One line in the game's output; deep recursive scripts (JSON
# parsers, for example) need to know this when they fail.
begin
  $__rgss_probe_depth = 0
  probe = lambda do |n|
    $__rgss_probe_depth = n
    probe.call(n + 1)
  end
  begin
    probe.call(0)
  rescue SystemStackError
    STDERR.puts "ruby recursion depth limit: #{$__rgss_probe_depth}"
  end
rescue StandardError
end

# Development aid: MKXP_DEBUG_SHOT=<file.png>,<frame> saves the picture at that frame and logs the frame count now and
# then, which shows whether a game is drawing, waiting or stuck.
if ENV['MKXP_DEBUG_SHOT'] && !Graphics.respond_to?(:__shot_update)
  path, frame = ENV['MKXP_DEBUG_SHOT'].split(',')
  $__shot_path = path
  $__shot_frame = frame.to_i
  module Graphics
    class << self
      alias_method :__shot_update, :update

      def update
        __shot_update
        n = Graphics.frame_count
        if n % 120 == 0
          now = Process.clock_gettime(Process::CLOCK_MONOTONIC)
          fps = $__shot_time ? (120 / (now - $__shot_time)).round(1) : 0
          $__shot_time = now
          STDERR.puts "frame #{n} fps=#{fps} scene=#{$scene.class} size=#{Graphics.width}x#{Graphics.height}"
          STDERR.puts "  at " + caller(1, 10).join("
  at ") if n % 600 == 0
        end
        if n == $__shot_frame
          begin
            Graphics.snap_to_bitmap.to_file($__shot_path)
            STDERR.puts "picture saved: #{$__shot_path}"
          rescue => e
            STDERR.puts "picture not saved: #{e}"
          end
        end
      end
    end
  end
end
