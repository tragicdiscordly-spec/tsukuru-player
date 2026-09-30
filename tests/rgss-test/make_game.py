#!/usr/bin/env python3
"""Generates a minimal RPG Maker XP style project (RGSS1) to test mkxp-z on the PS5.

  make_game.py OUTPUT_DIR

It contains Game.ini and Data/Scripts.rxdata: Ruby's Marshal format for [[id, name, deflated script]],
written by hand here so that no Ruby is needed on the PC.
"""
import os
import sys
import zlib

SCRIPT = r'''
puts "RGSS test: start (#{RUBY_VERSION}, #{RUBY_PLATFORM})"

background = Bitmap.new(640, 480)
background.fill_rect(0, 0, 640, 480, Color.new(30, 30, 90))
100.times { |i| background.fill_rect((i * 37) % 640, (i * 53) % 480, 24, 24, Color.new(60 + i, 90, 160, 255)) }
background.font.size = 32
background.draw_text(20, 20, 600, 40, "mkxp-z on PS5 - RGSS test")
bg_sprite = Sprite.new
bg_sprite.bitmap = background

boxes = []
40.times do |i|
  s = Sprite.new
  s.bitmap = Bitmap.new(32, 32)
  s.bitmap.fill_rect(0, 0, 32, 32, Color.new(255, 200 - i * 4, i * 6, 220))
  boxes << s
end

started = Time.now
frames = 0
300.times do |i|
  boxes.each_with_index do |s, n|
    s.x = 300 + 260 * Math.sin(i / 40.0 + n)
    s.y = 220 + 180 * Math.cos(i / 55.0 + n * 1.3)
  end
  Graphics.update
  Input.update
  frames += 1
  puts "frame #{frames}, Graphics.frame_count #{Graphics.frame_count}" if frames % 60 == 0
end
elapsed = Time.now - started
puts "RGSS test: #{frames} frames in #{'%.2f' % elapsed}s = #{'%.1f' % (frames / elapsed)} fps"

# MIDI: a scale on piano with a drum beat (Audio/BGM/scale.mid). Silent unless a SoundFont is set up.
puts "RGSS test: playing MIDI ..."
Audio.bgm_play("Audio/BGM/scale.mid", 100, 100)
started = Time.now
while Time.now - started < 9
  Graphics.update
  Input.update
end
Audio.bgm_stop
puts "RGSS test: done"
'''


def var_len(n):
    """MIDI variable-length quantity."""
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append((n & 0x7F) | 0x80)
        n >>= 7
    return bytes(reversed(out))


def scale_midi():
    """A C major scale on piano (channel 1) over a kick/snare/hi-hat beat (channel 10)."""
    ticks = 240  # an eighth note at 480 ticks per quarter note
    events = []  # (absolute tick, bytes)
    events.append((0, b'\xFF\x51\x03\x07\xA1\x20'))  # tempo: 500000 us per quarter note (120 bpm)
    events.append((0, b'\xC0\x00'))  # channel 1: piano
    t = 0
    for note in (60, 62, 64, 65, 67, 69, 71, 72, 71, 69, 67, 65, 64, 62, 60, 60):
        events.append((t, bytes([0x90, note, 100])))
        events.append((t + ticks - 20, bytes([0x80, note, 0])))
        t += ticks
    for beat in range(0, t, ticks):
        drum = (36, 42, 38, 42)[(beat // ticks) % 4]  # kick, hi-hat, snare, hi-hat
        events.append((beat, bytes([0x99, drum, 110])))
        events.append((beat + 100, bytes([0x89, drum, 0])))
    events.sort(key=lambda e: e[0])
    track = b''
    last = 0
    for tick, data in events:
        track += var_len(tick - last) + data
        last = tick
    track += var_len(0) + b'\xFF\x2F\x00'
    header = b'MThd' + (6).to_bytes(4, 'big') + (0).to_bytes(2, 'big') + (1).to_bytes(2, 'big') + (480).to_bytes(2, 'big')
    return header + b'MTrk' + len(track).to_bytes(4, 'big') + track


def m_int(n):
    """Marshal's compact integer encoding (non-negative values only)."""
    if n == 0:
        return b'\x00'
    if 0 < n < 123:
        return bytes([n + 5])
    for size in (1, 2, 3, 4):
        if n < 1 << (8 * size):
            return bytes([size]) + n.to_bytes(size, 'little')
    raise ValueError(n)


def m_string(data, utf8):
    if utf8:  # 'I' (has instance variables) '"' string, then ivar E => true
        return b'I"' + m_int(len(data)) + data + m_int(1) + b':' + m_int(1) + b'E' + b'T'
    return b'"' + m_int(len(data)) + data  # binary string


def scripts_rxdata(scripts):
    out = b'\x04\x08[' + m_int(len(scripts))
    for sid, name, source in scripts:
        packed = zlib.compress(source.encode('utf-8'))
        out += b'[' + m_int(3) + b'i' + m_int(sid) + m_string(name.encode(), True) + m_string(packed, False)
    return out


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(os.path.join(out, 'Data'), exist_ok=True)
    with open(os.path.join(out, 'Game.ini'), 'wb') as f:
        f.write(b'[Game]\r\nTitle=RGSS test\r\nScripts=Data\\Scripts.rxdata\r\n')
    with open(os.path.join(out, 'Data', 'Scripts.rxdata'), 'wb') as f:
        f.write(scripts_rxdata([(1, 'Main', SCRIPT)]))
    os.makedirs(os.path.join(out, 'Audio', 'BGM'), exist_ok=True)
    with open(os.path.join(out, 'Audio', 'BGM', 'scale.mid'), 'wb') as f:
        f.write(scale_midi())
    print('wrote', out)


if __name__ == '__main__':
    main()
