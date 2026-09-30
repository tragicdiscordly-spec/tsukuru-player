# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compatibility for games written for RPG Maker's Windows player (XP / VX / VX Ace), loaded before the game's own
# scripts (the launcher sets MKXP_PRELOAD to this file).
#
# * Zlib is part of RPG Maker's Ruby; here it has to be loaded.
# * Font.exist? says yes (fonts cannot be installed here).
# * Thread.critical (Ruby 1.8) does nothing.
# * Key state questions (GetAsyncKeyState) are answered from the console's keyboard and gamepad.
# * Win32API is the class games use to call Windows DLLs (user32, kernel32, steam_api, ...). There is no Windows here,
#   so a call does nothing and answers 0, which is what most games expect when an optional feature (Steam
#   achievements, window tricks, key state) is not available. A few harmless functions are answered properly. The
#   first call of every function is written to the game's output, so it is visible what a game asked for.
begin
  require 'zlib'
rescue LoadError
end

# Games such as Pokemon Essentials replace the Input module with one that asks Windows which keys are down
# (GetAsyncKeyState), so the gamepad would never reach them. The engine's own Input methods are kept under other
# names here, before the game replaces them, and those key questions are answered from them (see Win32API below).
module Input
  class << self
    alias_method :__native_update, :update
    alias_method :__native_press?, :press?
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

  def self.key_down?(vk)
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
    when /\AGetForegroundWindow/, /\AGetWindowThreadProcessId/
      return 1 # "this game's window is in front"
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
